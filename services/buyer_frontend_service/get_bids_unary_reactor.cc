//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/buyer_frontend_service/get_bids_unary_reactor.h"

#include <cstddef>
#include <optional>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/buyer_frontend_service/util/bidding_signals.h"
#include "services/buyer_frontend_service/util/proto_factory.h"
#include "services/common/chaffing/transcoding_utils.h"
#include "services/common/clients/kv_server/kv_v2.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/no_ops_logger.h"
#include "services/common/util/json_util.h"
#include "services/common/util/priority_vector/priority_vector_utils.h"
#include "services/common/util/request_metadata.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/buyer_input_proto_utils.h"

#include "kv_buyer_signals_adapter.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse;
using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;
using GenerateProtectedAppSignalsBidsRawResponse =
    GenerateProtectedAppSignalsBidsResponse::
        GenerateProtectedAppSignalsBidsRawResponse;

using KVLookUpResult =
    absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesResponse>>;

inline constexpr int kNumDefaultOutboundBiddingCalls = 1;

template <typename T>
void HandleSingleBidCompletion(
    absl::StatusOr<std::unique_ptr<T>> raw_response,
    absl::AnyInvocable<void(const absl::Status&) &&> on_error_response,
    absl::AnyInvocable<void() &&> on_empty_response,
    absl::AnyInvocable<void(std::unique_ptr<T>) &&> on_successful_response,
    GetBidsResponse::GetBidsRawResponse& get_bid_raw_response,
    bool& should_export_debug) {
  // Handle errors
  if (!raw_response.ok()) {
    std::move(on_error_response)(raw_response.status());
    return;
  }

  auto response = *std::move(raw_response);
  should_export_debug = response->bidding_export_debug();
  if (response->has_debug_info()) {
    server_common::DebugInfo& downstream_debug_info =
        *get_bid_raw_response.mutable_debug_info()->add_downstream_servers();
    downstream_debug_info = std::move(*response->mutable_debug_info());
    if constexpr (std::is_same_v<T,
                                 GenerateProtectedAppSignalsBidsRawResponse>) {
      downstream_debug_info.set_server_name("app_signal_bid");
    }
    if constexpr (std::is_same_v<
                      T, GenerateBidsResponse::GenerateBidsRawResponse>) {
      downstream_debug_info.set_server_name("bidding");
    }
  }

  // Handle empty response
  if (!response->IsInitialized() || response->bids_size() == 0) {
    std::move(on_empty_response)();
    return;
  }

  // Handle successful response
  std::move(on_successful_response)(std::move(response));
}

void LogIgMetric(const GetBidsRequest::GetBidsRawRequest& raw_request,
                 metric::BfeContext& metric_context) {
  int user_bidding_signals = 0;
  int bidding_signals_keys = 0;
  int device_signals = 0;
  int ad_render_ids = 0;
  int component_ads = 0;
  int igs_count = 0;
  for (const BuyerInputForBidding::InterestGroupForBidding& interest_group :
       raw_request.buyer_input_for_bidding().interest_groups()) {
    igs_count += 1;
    user_bidding_signals += interest_group.user_bidding_signals().size();
    for (const auto& bidding_signal_key :
         interest_group.bidding_signals_keys()) {
      bidding_signals_keys += bidding_signal_key.size();
    }
    for (const auto& ad_render_id : interest_group.ad_render_ids()) {
      ad_render_ids += ad_render_id.size();
    }
    for (const auto& component_ad : interest_group.component_ads()) {
      component_ads += component_ad.size();
    }
    if (raw_request.client_type() == CLIENT_TYPE_BROWSER) {
      device_signals += interest_group.browser_signals().ByteSizeLong();
    } else if (raw_request.client_type() == CLIENT_TYPE_ANDROID) {
      device_signals += interest_group.android_signals().ByteSizeLong();
    }
  }
  LogIfError(metric_context.LogHistogram<metric::kUserBiddingSignalsSize>(
      user_bidding_signals));
  LogIfError(metric_context.LogHistogram<metric::kBiddingSignalKeysSize>(
      bidding_signals_keys));
  LogIfError(
      metric_context.LogHistogram<metric::kAdRenderIDsSize>(ad_render_ids));
  LogIfError(
      metric_context.LogHistogram<metric::kComponentAdsSize>(component_ads));
  LogIfError(metric_context.LogHistogram<metric::kIGCount>(igs_count));
  LogIfError(
      metric_context.LogHistogram<metric::kDeviceSignalsSize>(device_signals));
}
}  // namespace

GetBidsUnaryReactor::GetBidsUnaryReactor(
    grpc::CallbackServerContext& context,
    const GetBidsRequest& get_bids_request, GetBidsResponse& get_bids_response,
    absl::Nullable<const BiddingSignalsAsyncProvider* const>
        bidding_signals_async_provider,
    BiddingAsyncClient& bidding_async_client, const GetBidsConfig& config,
    ProtectedAppSignalsBiddingAsyncClient* pas_bidding_async_client,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client, KVAsyncClient* kv_async_client,
    server_common::Executor& executor, bool enable_benchmarking)
    : context_(&context),
      request_(&get_bids_request),
      get_bids_response_(&get_bids_response),
      get_bids_raw_response_(
          std::make_unique<GetBidsResponse::GetBidsRawResponse>()),
      bidding_signals_async_provider_(bidding_signals_async_provider),
      kv_async_client_(kv_async_client),
      bidding_async_client_(&bidding_async_client),
      protected_app_signals_bidding_async_client_(pas_bidding_async_client),
      config_(config),
      key_fetcher_manager_(key_fetcher_manager),
      crypto_client_(crypto_client),
      chaffing_enabled_(config_.is_chaffing_enabled),
      is_sampled_for_debug_([this]() {
        decrypt_status_ = DecryptRequest();
        return SetGeneratorAndSample(
            config_.debug_sample_rate_micro,
            chaffing_enabled_ && raw_request_.is_chaff(),
            raw_request_.is_debug_eligible(),
            raw_request_.log_context().generation_id(), generator_);
      }()),
      log_context_([this]() {
        if (config_.consent_all_requests) {
          ModifyConsent(*raw_request_.mutable_consented_debug_config());
        }
        return RequestLogContext(
            GetLoggingContext(), raw_request_.consented_debug_config(),
            [this]() { return get_bids_raw_response_->mutable_debug_info(); },
            is_sampled_for_debug_);
      }()),
      async_task_tracker_(kNumDefaultOutboundBiddingCalls, log_context_,
                          [this](bool any_successful_bid) {
                            OnAllBidsDone(any_successful_bid);
                          }),
      enable_cancellation_(config.enable_cancellation),
      enable_enforce_kanon_(config.enable_kanon &&
                            raw_request_.enforce_kanon()),
      bidding_signals_fetch_mode_(config.bidding_signals_fetch_mode),
      executor_(executor) {
  if (enable_benchmarking) {
    std::string request_id = FormatTime(absl::Now());
    benchmarking_logger_ =
        std::make_unique<BuildInputProcessResponseBenchmarkingLogger>(
            request_id);
  } else {
    benchmarking_logger_ = std::make_unique<NoOpsLogger>();
  }
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BfeContextMap()->Remove(request_));
    if (log_context_.is_consented()) {
      metric_context_->SetConsented(raw_request_.log_context().generation_id());
    } else if (log_context_.is_prod_debug()) {
      metric_context_->SetConsented(kProdDebug.data());
    }
    return absl::OkStatus();
  }()) << "BfeContextMap()->Get(request) should have been called";

  DCHECK(!config_.is_protected_app_signals_enabled ||
         protected_app_signals_bidding_async_client_ != nullptr)
      << "PAS is enabled but no PAS bidding async client available";
}

GetBidsUnaryReactor::GetBidsUnaryReactor(
    grpc::CallbackServerContext& context,
    const GetBidsRequest& get_bids_request, GetBidsResponse& get_bids_response,
    absl::Nullable<const BiddingSignalsAsyncProvider* const>
        bidding_signals_async_provider,
    BiddingAsyncClient& bidding_async_client, const GetBidsConfig& config,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client, KVAsyncClient* kv_async_client,
    server_common::Executor& executor, bool enable_benchmarking)
    : GetBidsUnaryReactor(context, get_bids_request, get_bids_response,
                          bidding_signals_async_provider, bidding_async_client,
                          config, /*pas_bidding_async_client=*/nullptr,
                          key_fetcher_manager, crypto_client, kv_async_client,
                          executor, enable_benchmarking) {}

void GetBidsUnaryReactor::OnAllBidsDone(bool any_successful_bids) {
  if (enable_cancellation_ && context_->IsCancelled()) {
    benchmarking_logger_->End();
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  if (!any_successful_bids) {
    PS_LOG(WARNING, log_context_)
        << "Finishing the GetBids RPC with an error, since there are "
           "no successful bids returned by the bidding service";
    benchmarking_logger_->End();
    FinishWithStatus(
        grpc::Status(grpc::INTERNAL, absl::StrJoin(bid_errors_, "; ")));
    return;
  }

  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "GetBidsRawResponse exported in EventMessage if consented";
    log_context_.SetEventMessageField(*get_bids_raw_response_);
  }
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true,
                                  should_export_debug_);
  if (auto encryption_status = EncryptResponse(); !encryption_status.ok()) {
    PS_LOG(ERROR, log_context_) << "Failed to encrypt the response";
    benchmarking_logger_->End();
    FinishWithStatus(
        grpc::Status(grpc::StatusCode::INTERNAL, encryption_status.ToString()));
    return;
  }

  PS_VLOG(kEncrypted, log_context_) << "Encrypted GetBidsResponse:\n"
                                    << get_bids_response_->ShortDebugString();

  benchmarking_logger_->End();
  FinishWithStatus(grpc::Status::OK);
}

grpc::Status GetBidsUnaryReactor::ParseRawRequestBytestring(
    HpkeDecryptResponse& decrypt_response) {
  absl::StatusOr<CompressionType> compression_type = GetCompressionType();
  if (!compression_type.ok()) {
    return {grpc::StatusCode::INVALID_ARGUMENT,
            compression_type.status().message().data()};
  }

  // compression_type defaults to kUncompressed if no header is provided in the
  // request.
  if (*compression_type != CompressionType::kUncompressed) {
    // If compression_type_ is NOT kUncompressed, this is a request following
    // the old request format and is compressed.
    compression_type_ = *compression_type;
    absl::StatusOr<std::string> decompressed = Decompress(
        std::move(*decrypt_response.mutable_payload()), compression_type_);
    if (!decompressed.ok()) {
      PS_LOG(ERROR) << "Failed to decompress request: "
                    << decompressed.status();
      return {grpc::StatusCode::INVALID_ARGUMENT, kMalformedRequest};
    }

    PS_VLOG(kStats) << "Decompressed payload size: " << decompressed->length();
    if (!raw_request_.ParseFromString(*decompressed)) {
      return {grpc::StatusCode::INVALID_ARGUMENT, kMalformedRequest};
    }
  } else if (chaffing_enabled_ &&
             ((decrypt_response.payload().front() == '\0') ||
              (decrypt_response.payload().front() == '\x01'))) {
    PS_VLOG(9) << "Decoding request according to new request format";
    // If the payload begins with the null terminator, the request is in the
    // the new request format.

    // Save that the request is following the new format into
    // use_new_payload_encoding_; the response will also use the new format.
    use_new_payload_encoding_ = true;
    absl::StatusOr<DecodedGetBidsPayload<GetBidsRequest::GetBidsRawRequest>>
        decoded_payload =
            DecodeGetBidsPayload<GetBidsRequest::GetBidsRawRequest>(
                decrypt_response.payload());
    if (!decoded_payload.ok()) {
      PS_LOG(ERROR) << "Failed to decode request: " << decoded_payload.status();
      return {grpc::StatusCode::INVALID_ARGUMENT, kMalformedCiphertext};
    }

    compression_type_ = decoded_payload->compression_type;
    if (decoded_payload->version != 0) {
      // For now, we don't support any version/compression bytes besides 0.
      return {grpc::StatusCode::INVALID_ARGUMENT, kUnsupportedMetadataValues};
    }

    raw_request_ = std::move(decoded_payload->get_bids_proto);

    PS_VLOG(kStats) << "Compression type: " << ((int)compression_type_);
    PS_VLOG(kStats) << "Decoded/Decompressed payload size: "
                    << raw_request_.SerializeAsString().length();
  } else {
    if (!raw_request_.ParseFromString(decrypt_response.payload())) {
      // If not, try to parse the request as before.
      return {grpc::StatusCode::INVALID_ARGUMENT, kMalformedRequest};
    }

    compression_type_ = *compression_type;  // kUncompressed
  }

  return grpc::Status::OK;
}

grpc::Status GetBidsUnaryReactor::DecryptRequest() {
  if (request_->key_id().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT, kEmptyKeyIdError};
  }

  if (request_->request_ciphertext().empty()) {
    return {grpc::StatusCode::INVALID_ARGUMENT, kEmptyCiphertextError};
  }

  std::optional<server_common::PrivateKey> private_key =
      key_fetcher_manager_->GetPrivateKey(request_->key_id());
  if (!private_key) {
    return {grpc::StatusCode::INVALID_ARGUMENT, kInvalidKeyIdError};
  }

  absl::StatusOr<HpkeDecryptResponse> decrypt_response =
      crypto_client_->HpkeDecrypt(*private_key, request_->request_ciphertext());
  if (!decrypt_response.ok()) {
    return {grpc::StatusCode::INVALID_ARGUMENT, kMalformedCiphertext};
  }

  PS_VLOG(kStats) << "Decrypted payload size: "
                  << decrypt_response->payload().length();
  hpke_secret_ = std::move(*decrypt_response->mutable_secret());

  return ParseRawRequestBytestring(*decrypt_response);
}

int GetBidsUnaryReactor::GetNumberOfMaximumBiddingCalls() {
  int num_expected_calls = 0;
  if (config_.is_protected_audience_enabled &&
      (!raw_request_.buyer_input().interest_groups().empty() ||
       !raw_request_.buyer_input_for_bidding().interest_groups().empty())) {
    PS_VLOG(5, log_context_) << "Interest groups found in the request";
    ++num_expected_calls;
  }

  if (config_.is_protected_app_signals_enabled &&
      raw_request_.has_protected_app_signals_buyer_input()) {
    PS_VLOG(5, log_context_) << "Protected app signals found in the request";
    ++num_expected_calls;
  }
  return num_expected_calls;
}

void GetBidsUnaryReactor::CancellableExecute() {
  benchmarking_logger_->Begin();
  if (server_common::log::PS_VLOG_IS_ON(kEncrypted)) {
    PS_VLOG(kEncrypted, log_context_)
        << "Encrypted GetBidsRequest exported in EventMessage if consented";
    log_context_.SetEventMessageField(*request_);
  }
  PS_VLOG(kPlain, log_context_)
      << "Headers:\n"
      << absl::StrJoin(context_->client_metadata(), "\n",
                       absl::PairFormatter(absl::StreamFormatter(), " : ",
                                           absl::StreamFormatter()));

  // TODO(b/278039901): Add integration test for metadata forwarding.
  absl::StatusOr<RequestMetadata> kv_metadata = GrpcMetadataToRequestMetadata(
      context_->client_metadata(), kBuyerKVMetadata);
  if (!kv_metadata.ok()) {
    PS_VLOG(kNoisyWarn, log_context_) << kv_metadata.status();
    FinishWithStatus(server_common::FromAbslStatus(kv_metadata.status()));
    return;
  }

  absl::StatusOr<RequestMetadata> bidding_metadata =
      GrpcMetadataToRequestMetadata(context_->client_metadata(),
                                    kBiddingMetadata);
  if (!bidding_metadata.ok()) {
    PS_VLOG(kNoisyWarn, log_context_) << bidding_metadata.status();
    FinishWithStatus(server_common::FromAbslStatus(bidding_metadata.status()));
    return;
  }

  kv_metadata_ = *std::move(kv_metadata);
  bidding_metadata_ = *std::move(bidding_metadata);

  if (!decrypt_status_.ok()) {
    PS_LOG(ERROR, log_context_) << "Decrypting the request failed:"
                                << server_common::ToAbslStatus(decrypt_status_);
    FinishWithStatus(decrypt_status_);
    return;
  }
  PS_VLOG(5, log_context_) << "Successfully decrypted the request";
  if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
    PS_VLOG(kPlain, log_context_)
        << "GetBidsRawRequest exported in EventMessage if consented";
    log_context_.SetEventMessageField(raw_request_);
  }

  LogIgMetric(raw_request_, *metric_context_);

  if (chaffing_enabled_ && raw_request_.is_chaff()) {
    ExecuteChaffRequest();
    return;
  }

  if (raw_request_.has_buyer_input()) {
    *raw_request_.mutable_buyer_input_for_bidding() =
        ToBuyerInputForBidding(raw_request_.buyer_input());
  }

  const int num_bidding_calls = GetNumberOfMaximumBiddingCalls();
  if (num_bidding_calls == 0) {
    // This is unlikely to happen since we already have this check in place
    // in SFE.
    PS_LOG(ERROR, log_context_) << "No protected audience or protected app "
                                   "signals input found in the request";
    benchmarking_logger_->End();
    FinishWithStatus(grpc::Status(grpc::INVALID_ARGUMENT, kMissingInputs));
    return;
  }

  if (config_.priority_vector_enabled &&
      !raw_request_.priority_signals().empty()) {
    absl::StatusOr<rapidjson::Document> priority_signals =
        ParsePriorityVector(raw_request_.priority_signals());
    if (!priority_signals.ok()) {
      FinishWithStatus(grpc::Status(grpc::INVALID_ARGUMENT, kMissingInputs));
      return;
    }

    priority_signals_vector_ = *std::move(priority_signals);
  }

  async_task_tracker_.SetNumTasksToTrack(num_bidding_calls);
  MayGetProtectedAudienceBids();
  MayGetProtectedSignalsBids();
}

void GetBidsUnaryReactor::CancellableExecuteChaffRequest() {
  // Sleep to make it seem (from the client's perspective) that the BFE is
  // processing the request.
  size_t chaff_request_duration = 0;
  std::uniform_int_distribution<size_t> request_duration_dist(
      kMinChaffRequestDurationMs, kMaxChaffRequestDurationMs);
  if (generator_.has_value()) {
    chaff_request_duration = request_duration_dist(*generator_);
  }
  // Produce chaff response.
  size_t chaff_response_size = 0;
  std::uniform_int_distribution<size_t> chaff_response_size_dist(
      kMinChaffResponseSizeBytes, kMaxChaffResponseSizeBytes);
  if (generator_.has_value()) {
    chaff_response_size = chaff_response_size_dist(*generator_);
  }
  absl::StatusOr<std::string> encoded_payload = EncodeAndCompressGetBidsPayload(
      *get_bids_raw_response_, compression_type_, chaff_response_size);
  if (!encoded_payload.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to encode response: " << encoded_payload.status();
    FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
    return;
  }

  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
      aead_encrypt =
          crypto_client_->AeadEncrypt(*encoded_payload, hpke_secret_);
  if (!aead_encrypt.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to encrypt chaff response: " << aead_encrypt.status();
    FinishWithStatus(grpc::Status(grpc::INTERNAL, kInternalServerError));
    return;
  }

  PS_VLOG(kNoisyInfo, log_context_) << "Chaff response encrypted successfully";
  get_bids_response_->set_response_ciphertext(
      aead_encrypt->encrypted_data().ciphertext());

  executor_.RunAfter(absl::Milliseconds((int)chaff_request_duration),
                     [this]() { FinishWithStatus(grpc::Status::OK); });
}

void GetBidsUnaryReactor::LogInitiatedRequestErrorMetrics(
    absl::string_view server_name, const absl::Status& status) {
  if (server_name == metric::kKv) {
    LogIfError(
        metric_context_
            ->AccumulateMetric<metric::kInitiatedRequestKVErrorCountByStatus>(
                1, (StatusCodeToString(status.code()))));
  } else if (server_name == metric::kBs) {
    LogIfError(metric_context_->AccumulateMetric<
               metric::kInitiatedRequestBiddingErrorCountByStatus>(
        1, (StatusCodeToString(status.code()))));
  }
}

void GetBidsUnaryReactor::MayGetProtectedSignalsBids() {
  if (!config_.is_protected_app_signals_enabled) {
    PS_VLOG(8, log_context_) << "Protected App Signals feature not enabled";
    return;
  }

  if (!raw_request_.has_protected_app_signals_buyer_input() ||
      !raw_request_.protected_app_signals_buyer_input()
           .has_protected_app_signals()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No protected app buyer signals input found, skipping fetching bids "
           "for protected app signals";
    return;
  }

  auto protected_app_signals_bid_request =
      CreateGenerateProtectedAppSignalsBidsRawRequest(raw_request_,
                                                      enable_enforce_kanon_);
  protected_app_signals_bid_request->set_is_sampled_for_debug(
      is_sampled_for_debug_);

  grpc::ClientContext* client_context = client_contexts_.Add(bidding_metadata_);

  absl::Status execute_result =
      protected_app_signals_bidding_async_client_->ExecuteInternal(
          std::move(protected_app_signals_bid_request), client_context,
          [this](
              absl::StatusOr<
                  std::unique_ptr<GenerateProtectedAppSignalsBidsRawResponse>>
                  raw_response,
              ResponseMetadata response_metadata) {
            HandleSingleBidCompletion<
                GenerateProtectedAppSignalsBidsRawResponse>(
                std::move(raw_response),
                // Error response handler
                [this](const absl::Status& status) {
                  LogIfError(metric_context_->AccumulateMetric<
                             metric::kBfeErrorCountByErrorCode>(
                      1,
                      metric::
                          kBfeGenerateProtectedAppSignalsBidsResponseError));  // NOLINT
                  PS_LOG(ERROR, log_context_)
                      << "Execution of GenerateProtectedAppSignalsBids "
                         "request "
                         "failed with status: "
                      << status;
                  async_task_tracker_.TaskCompleted(
                      TaskStatus::ERROR, [this, &status]() {
                        bid_errors_.push_back(status.ToString());
                      });
                },
                // Empty response handler
                [this]() {
                  async_task_tracker_.TaskCompleted(
                      TaskStatus::EMPTY_RESPONSE, [this]() {
                        get_bids_raw_response_
                            ->mutable_protected_app_signals_bids();
                      });
                },
                // Successful response handler
                CancellationWrapper(
                    context_, enable_cancellation_,
                    [this](auto response) {
                      async_task_tracker_.TaskCompleted(
                          TaskStatus::SUCCESS,
                          [this, response = std::move(response)]() {
                            get_bids_raw_response_
                                ->mutable_protected_app_signals_bids()
                                ->Swap(response->mutable_bids());
                          });
                    },
                    [&async_task_tracker_ =
                         async_task_tracker_]() {  // OnCancel
                      async_task_tracker_.TaskCompleted(TaskStatus::CANCELLED);
                    }),
                *get_bids_raw_response_, should_export_debug_);
          },
          absl::Milliseconds(
              config_.protected_app_signals_generate_bid_timeout_ms));
  if (!execute_result.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kBfeErrorCountByErrorCode>(
            1, metric::kBfeGenerateProtectedAppSignalsBidsFailedToCall));
    PS_LOG(ERROR, log_context_)
        << "Failed to make async GenerateProtectedAppInstallBids call: (error: "
        << execute_result.ToString() << ")";
    async_task_tracker_.TaskCompleted(
        TaskStatus::ERROR, [this, &execute_result]() {
          bid_errors_.push_back(execute_result.ToString());
        });
  }
}

void GetBidsUnaryReactor::MayGetProtectedAudienceBidsV1(
    const BiddingSignalsRequest& bidding_signals_request, bool is_hybrid) {
  if (bidding_signals_fetch_mode_ == BiddingSignalsFetchMode::NOT_FETCHED) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckBiddingSignalsFetchFlagV1ErrorMsg));
    return;
  }
  if (bidding_signals_async_provider_ == nullptr) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckBiddingSignalsProviderV1ErrorMsg));
    return;
  }
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get())
          .release();
  // Get Bidding Signals.
  bidding_signals_async_provider_->Get(
      bidding_signals_request,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, kv_request, is_hybrid, bidding_signals_request](
              absl::StatusOr<std::unique_ptr<BiddingSignals>> response,
              GetByteSize get_byte_size) mutable {
            {
              // Only logs KV request and response sizes if fetching signals
              // succeeds.
              if (response.ok()) {
                kv_request->SetRequestSize(get_byte_size.request);
                kv_request->SetResponseSize(get_byte_size.response);
              }
              // destruct kv_request, destructor measures request time
              delete kv_request;
            }
            if (!response.ok()) {
              LogIfError(
                  metric_context_
                      ->AccumulateMetric<metric::kBfeErrorCountByErrorCode>(
                          1, metric::kBfeBiddingSignalsResponseError));
              LogInitiatedRequestErrorMetrics(metric::kKv, response.status());
              // Return error to client.
              PS_LOG(ERROR, log_context_)
                  << "GetBiddingSignals request failed with status:"
                  << response.status();
              async_task_tracker_.TaskCompleted(
                  TaskStatus::ERROR, [this, &response]() {
                    bid_errors_.push_back(response.status().ToString());
                  });
              return;
            }
            if (is_hybrid && !(*response)->is_hybrid_v1_return) {
              MayGetProtectedAudienceBidsV2(
                  bidding_signals_request,
                  std::move((*response)->trusted_signals));
              return;
            }

            // Sends protected audience bid request to bidding service.
            PrepareAndGenerateProtectedAudienceBid(*std::move(response));
          },
          [this, kv_request]() {
            delete kv_request;
            async_task_tracker_.TaskCompleted(TaskStatus::CANCELLED);
          }),
      absl::Milliseconds(config_.bidding_signals_load_timeout_ms),
      {log_context_});
}

void GetBidsUnaryReactor::HandleV2Failure(const absl::Status& status,
                                          absl::string_view error_message,
                                          EventMessage::KvSignal bid_signal) {
  LogIfError(
      metric_context_->AccumulateMetric<metric::kBfeErrorCountByErrorCode>(
          1, metric::kBfeBiddingSignalsResponseError));
  LogInitiatedRequestErrorMetrics(metric::kKv, status);
  // Return error to client.
  PS_LOG(ERROR, log_context_) << error_message << status;
  async_task_tracker_.TaskCompleted(TaskStatus::ERROR, [this, &status]() {
    bid_errors_.push_back(status.ToString());
  });
  if (server_common::log::PS_VLOG_IS_ON(kKVLog)) {
    log_context_.SetEventMessageField(std::move(bid_signal));
  }
}

void GetBidsUnaryReactor::MayGetProtectedAudienceBidsV2(
    const BiddingSignalsRequest& bidding_signals_request,
    std::unique_ptr<std::string> byos_output) {
  if (bidding_signals_fetch_mode_ == BiddingSignalsFetchMode::NOT_FETCHED) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckBiddingSignalsFetchFlagV2ErrorMsg));
    return;
  }
  if (kv_async_client_ == nullptr) {
    FinishWithStatus(grpc::Status(grpc::StatusCode::INTERNAL,
                                  kCheckBiddingSignalsProviderV2ErrorMsg));
    return;
  }
  auto kv_request =
      metric::MakeInitiatedRequest(metric::kKv, metric_context_.get())
          .release();
  grpc::ClientContext* client_context = client_contexts_.Add();
  auto maybe_bidding_signals_request = CreateV2BiddingRequest(
      bidding_signals_request, config_.propagate_buyer_signals_to_tkv,
      std::move(byos_output));
  if (!maybe_bidding_signals_request.ok()) {
    PS_VLOG(kNoisyWarn, log_context_) << "Failed creating TKV bidding request. "
                                      << maybe_bidding_signals_request.status();
    return;
  }
  EventMessage::KvSignal bid_signal = KvEventMessage(
      (*maybe_bidding_signals_request)->ShortDebugString(), log_context_);

  auto status = kv_async_client_->ExecuteInternal(
      *std::move(maybe_bidding_signals_request), client_context,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, kv_request, bid_signal = std::move(bid_signal)](
              KVLookUpResult kv_look_up_result,
              ResponseMetadata response_metadata) mutable {
            {
              // Only logs KV request and response sizes if fetching signals
              // succeeds.
              if (kv_look_up_result.ok()) {
                kv_request->SetRequestSize(response_metadata.request_size);
                kv_request->SetResponseSize(response_metadata.response_size);
              }
              // destruct kv_request, destructor measures request time
              delete kv_request;
            }
            if (!kv_look_up_result.ok()) {
              HandleV2Failure(kv_look_up_result.status(),
                              "GetBiddingSignals request failed with status:",
                              std::move(bid_signal));
              return;
            }
            KVV2AdapterStats v2_adapter_stats;
            auto signals = ConvertV2BiddingSignalsToV1(
                *std::move(kv_look_up_result), v2_adapter_stats);
            PS_VLOG(kStats, log_context_)
                << "Number of values with additional json string parsing "
                   "applied:"
                << v2_adapter_stats.values_with_json_string_parsing
                << " and without additional json string parsing applied:"
                << v2_adapter_stats.values_without_json_string_parsing;

            if (!signals.ok()) {
              HandleV2Failure(signals.status(),
                              "Failed converting TKV response. ",
                              std::move(bid_signal));
              return;
            }
            SetKvEventMessage("KVAsyncGrpcClient",
                              *((*signals)->trusted_signals),
                              std::move(bid_signal), log_context_);
            // Sends protected audience bid request to bidding service.
            PrepareAndGenerateProtectedAudienceBid(*std::move(signals));
          },
          [this, kv_request]() {
            delete kv_request;
            async_task_tracker_.TaskCompleted(TaskStatus::CANCELLED);
          }),
      absl::Milliseconds(config_.bidding_signals_load_timeout_ms));
  if (!status.ok()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Failed to execute ads metadata KV lookup request: " << status;
    async_task_tracker_.TaskCompleted(TaskStatus::ERROR, [this, &status]() {
      bid_errors_.push_back(status.ToString());
    });
  }
}

void GetBidsUnaryReactor::MayGetProtectedAudienceBids() {
  if (!config_.is_protected_audience_enabled) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Protected Audience is not enabled, skipping bids fetching for PA";
    return;
  }

  if (raw_request_.buyer_input().interest_groups().empty() &&
      raw_request_.buyer_input_for_bidding().interest_groups().empty()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "No interest groups found, skipping bidding for protected audience";
    return;
  }

  if (bidding_signals_fetch_mode_ != BiddingSignalsFetchMode::NOT_FETCHED) {
    BiddingSignalsRequest bidding_signals_request(raw_request_, kv_metadata_);
    if (UseKvV2(raw_request_.client_type(), config_.is_tkv_v2_browser_enabled,
                config_.test_mode, config_.tkv_v2_address_empty)) {
      MayGetProtectedAudienceBidsV2(bidding_signals_request);
    } else if (config_.is_hybrid_enabled &&
               raw_request_.client_type() == CLIENT_TYPE_BROWSER) {
      MayGetProtectedAudienceBidsV1(bidding_signals_request,
                                    /* is_hybrid*/ true);
    } else {
      MayGetProtectedAudienceBidsV1(bidding_signals_request);
    }
  } else {
    PrepareAndGenerateProtectedAudienceBid(std::make_unique<BiddingSignals>());
  }
}

// Process Outputs from Actions to prepare bidding request.
// All Preload actions must have completed before this is invoked.
void GetBidsUnaryReactor::PrepareAndGenerateProtectedAudienceBid(
    std::unique_ptr<BiddingSignals> bidding_signals) {
  auto start_deserialize_time = absl::Now();
  uint32_t data_version = bidding_signals->data_version;
  absl::StatusOr<BiddingSignalJsonComponents> parsed_bidding_signals =
      ParseTrustedBiddingSignals(std::move(bidding_signals),
                                 raw_request_.buyer_input_for_bidding());
  if (parsed_bidding_signals.ok()) {
    get_bids_raw_response_->mutable_update_interest_group_list()->Swap(
        &(parsed_bidding_signals->update_igs));

    PS_VLOG(kStats, log_context_)
        << "\nTrusted Bidding Signals Deserialize Time: "
        << ToInt64Microseconds((absl::Now() - start_deserialize_time))
        << " microseconds for " << (*parsed_bidding_signals).raw_size
        << " bytes.";

    bidding_signal_json_components_ = std::move(*parsed_bidding_signals);
  } else if (bidding_signals_fetch_mode_ == BiddingSignalsFetchMode::REQUIRED) {
    PS_LOG(ERROR, log_context_) << parsed_bidding_signals.status();
    if (parsed_bidding_signals.status().code() ==
        absl::StatusCode::kInvalidArgument) {
      async_task_tracker_.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
    } else {
      async_task_tracker_.TaskCompleted(
          TaskStatus::ERROR, [this, &parsed_bidding_signals]() {
            bid_errors_.push_back(parsed_bidding_signals.status().ToString());
          });
    }
    return;
  }

  PriorityVectorConfig pv_config = {
      .priority_vector_enabled = config_.priority_vector_enabled,
      .priority_signals = priority_signals_vector_,
      .per_ig_priority_vectors =
          bidding_signal_json_components_.per_ig_priority_vectors};

  PrepareGenerateBidsRequestResult result = PrepareGenerateBidsRequest(
      raw_request_, std::move(bidding_signal_json_components_.bidding_signals),
      bidding_signal_json_components_.raw_size, data_version, pv_config,
      {.enable_kanon = enable_enforce_kanon_,
       .require_bidding_signals =
           bidding_signals_fetch_mode_ == BiddingSignalsFetchMode::REQUIRED});
  LogIfError(metric_context_->LogHistogram<metric::kPercentIgsFiltered>(
      static_cast<double>(result.percent_igs_filtered)));
  std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
      raw_bidding_input = std::move(result.raw_request);
  raw_bidding_input->set_is_sampled_for_debug(is_sampled_for_debug_);
  PS_VLOG(kOriginated, log_context_) << "GenerateBidsRequest:\n"
                                     << raw_bidding_input->ShortDebugString();
  if (raw_bidding_input->interest_group_for_bidding_size() == 0) {
    PS_LOG(INFO, log_context_)
        << "No interest groups for bidding found in the request.";
    async_task_tracker_.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
    return;
  }

  auto bidding_request =
      metric::MakeInitiatedRequest(metric::kBs, metric_context_.get())
          .release();
  bidding_request->SetRequestSize((int)raw_bidding_input->ByteSizeLong());
  grpc::ClientContext* client_context = client_contexts_.Add();
  absl::Status execute_result = bidding_async_client_->ExecuteInternal(
      std::move(raw_bidding_input), client_context,
      [this, bidding_request](
          absl::StatusOr<
              std::unique_ptr<GenerateBidsResponse::GenerateBidsRawResponse>>
              raw_response,
          ResponseMetadata response_metadata) mutable {
        {
          int response_size =
              raw_response.ok() ? (int)raw_response->get()->ByteSizeLong() : 0;
          bidding_request->SetResponseSize(response_size);
          // destruct bidding_request, destructor measures request time
          delete bidding_request;
        }
        HandleSingleBidCompletion<
            GenerateBidsResponse::GenerateBidsRawResponse>(
            std::move(raw_response),
            // Error response handler
            [this](const absl::Status& status) {
              LogIfError(
                  metric_context_
                      ->AccumulateMetric<metric::kBfeErrorCountByErrorCode>(
                          1, metric::kBfeGenerateBidsResponseError));
              LogInitiatedRequestErrorMetrics(metric::kBs, status);
              PS_LOG(ERROR, log_context_) << "Execution of GenerateBids "
                                             "request failed with status: "
                                          << status;
              async_task_tracker_.TaskCompleted(
                  TaskStatus::ERROR, [this, &status]() {
                    bid_errors_.push_back(status.ToString());
                  });
            },
            // Empty response handler
            [this]() {
              async_task_tracker_.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
            },
            // Successful response handler
            CancellationWrapper(
                context_, enable_cancellation_,
                [this](auto response) {
                  async_task_tracker_.TaskCompleted(
                      TaskStatus::SUCCESS,
                      [this, response = std::move(response)]() {
                        get_bids_raw_response_->mutable_bids()->Swap(
                            response->mutable_bids());
                      });
                },
                [&async_task_tracker_ = async_task_tracker_]() {  // OnCancel
                  async_task_tracker_.TaskCompleted(TaskStatus::CANCELLED);
                }),
            *get_bids_raw_response_, should_export_debug_);
      },
      absl::Milliseconds(config_.generate_bid_timeout_ms));
  if (!execute_result.ok()) {
    LogIfError(
        metric_context_->AccumulateMetric<metric::kBfeErrorCountByErrorCode>(
            1, metric::kBfeGenerateBidsFailedToCall));
    PS_LOG(ERROR, log_context_)
        << "Failed to make async GenerateBids call: (error: "
        << execute_result.ToString() << ")";
    async_task_tracker_.TaskCompleted(
        TaskStatus::ERROR, [this, &execute_result]() {
          bid_errors_.push_back(execute_result.ToString());
        });
  }
}

absl::Status GetBidsUnaryReactor::EncryptResponse() {
  std::string payload;
  if (use_new_payload_encoding_) {
    absl::StatusOr<std::string> encoded_payload =
        EncodeAndCompressGetBidsPayload(*get_bids_raw_response_,
                                        CompressionType::kGzip);
    if (!encoded_payload.ok()) {
      PS_LOG(ERROR, log_context_)
          << "Failed to encode/compress response: " << encoded_payload.status();
      return encoded_payload.status();
    }

    payload = *std::move(encoded_payload);
  } else {
    PS_VLOG(9, log_context_) << "Using old response format";
    payload = get_bids_raw_response_->SerializeAsString();
    PS_VLOG(kStats, log_context_) << "compression_type_: " << compression_type_;
    PS_VLOG(kStats, log_context_)
        << "response payload size before compression: " << payload.length();
    PS_ASSIGN_OR_RETURN(std::string compressed_payload,
                        Compress(std::move(payload), compression_type_));
    PS_VLOG(kStats, log_context_) << "Response payload size after compression: "
                                  << compressed_payload.length();
    payload = std::move(compressed_payload);
  }

  PS_ASSIGN_OR_RETURN(auto aead_encrypt,
                      crypto_client_->AeadEncrypt(payload, hpke_secret_));

  get_bids_response_->set_response_ciphertext(
      aead_encrypt.encrypted_data().ciphertext());
  return absl::OkStatus();
}

absl::btree_map<std::string, std::string>
GetBidsUnaryReactor::GetLoggingContext() {
  const auto& log_context = raw_request_.log_context();
  return {{kGenerationId, log_context.generation_id()},
          {kBuyerDebugId, log_context.adtech_debug_id()}};
}

void GetBidsUnaryReactor::FinishWithStatus(const grpc::Status& status) {
  if (status.error_code() != grpc::StatusCode::OK) {
    metric_context_->SetRequestResult(server_common::ToAbslStatus(status));
  }
  Finish(status);
}

absl::StatusOr<CompressionType> GetBidsUnaryReactor::GetCompressionType() {
  int compression_type_num = 0;
  auto compression_type_it =
      context_->client_metadata().find(kBiddingAuctionCompressionHeader.data());
  if (compression_type_it != context_->client_metadata().end()) {
    PS_VLOG(8) << "B&A compression header found in request";
    std::string compression_type_str(compression_type_it->second.begin(),
                                     compression_type_it->second.end());
    if (!absl::SimpleAtoi(compression_type_str, &compression_type_num) ||
        !ToCompressionType(compression_type_num).ok()) {
      return absl::InvalidArgumentError(kInvalidCompressionHeaderValue);
    }
  }

  PS_VLOG(8) << "Compression type from examining request headers: "
             << compression_type_num;
  return ToCompressionType(compression_type_num);
}

// Deletes all data related to this object.
void GetBidsUnaryReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
