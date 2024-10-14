// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>

#include "absl/flags/flag.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "public/applications/pas/retrieval_request_builder.h"
#include "public/applications/pas/retrieval_response_parser.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/utils/egress.h"
#include "services/bidding_service/utils/validation.h"
#include "services/common/feature_flags.h"
#include "services/common/util/cancellation_wrapper.h"
#include "services/common/util/error_categories.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_metadata.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using KVLookUpResult =
    absl::StatusOr<std::unique_ptr<kv_server::v2::GetValuesResponse>>;

inline constexpr char kClientIpKey[] = "x-bna-client-ip";
inline constexpr char kUserAgentKey[] = "x-user-agent";
inline constexpr char kAcceptLanguageKey[] = "x-accept-language";
inline constexpr char kUserAgent[] = "X-User-Agent";
inline constexpr char kClientIp[] = "X-BnA-Client-IP";
inline constexpr char kAcceptLanguage[] = "X-Accept-Language";
// The egress protocol version should be in: [0, 31]
inline constexpr uint32_t kDefaultEgressProtocol = 0;
inline constexpr uint32_t kEgressProtocolBitWidth = 5;
// Hardcoded egress schema version till we support multi-version model for
// egress schema.
inline constexpr uint32_t kDefaultEgressSchemaVersion = 2;

inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
    kBuyerMetadataKeysMap = {{{kAcceptLanguageKey, kAcceptLanguageKey},
                              {kUserAgentKey, kUserAgentKey},
                              {kClientIpKey, kClientIpKey}}};

inline void PopulateArgInRomaRequest(
    absl::string_view arg, int index,
    std::vector<std::shared_ptr<std::string>>& request) {
  request[index] = std::make_shared<std::string>((arg.empty()) ? "\"\"" : arg);
}

absl::Status PopulateValuesForEgressFeatures(
    rapidjson::Value&& values,
    std::vector<std::unique_ptr<EgressFeature>>& egress_features) {
  PS_ASSIGN_OR_RETURN(auto feature_array, GetArrayMember(values, "features"));
  if (feature_array.Size() != egress_features.size()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Number of values in the egress payload: ", feature_array.Size(),
        " doesn't match with egress "
        "schema: ",
        egress_features.size(), " (note: nullability is not supported yet)"));
  }
  int idx = 0;
  for (rapidjson::Value& feat : feature_array) {
    if (!feat.IsObject()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Feature in egress payload at index ", idx,
                       " is not an object, expected object"));
    }
    PS_VLOG(5) << "Setting the value using payload for feature at idx: " << idx;
    PS_RETURN_IF_ERROR(egress_features[idx]->SetValue(std::move(feat)))
        << "Failed to set value for feature at idx: " << idx;
    ++idx;
  }
  return absl::OkStatus();
}

uint8_t GetEgressVectorHeader(uint32_t schema_version) {
  DCHECK_LE(schema_version, 7) << "Only 8 schema versions are supported";
  uint8_t header = schema_version << kEgressProtocolBitWidth;
  header |= kDefaultEgressProtocol;
  return header;
}

// Returns all serialized features as a Base64 encoded byte string.
absl::StatusOr<std::string> SerializeEgressFeatures(
    uint32_t schema_version,
    const std::vector<std::unique_ptr<EgressFeature>>& egress_features,
    uint32_t egress_bit_limit) {
  uint32_t total_size = 0;
  for (const auto& egress_feature : egress_features) {
    PS_VLOG(5) << "Size of feature: " << egress_feature->Size();
    total_size += egress_feature->Size();
  }
  PS_VLOG(5) << "Total size (without padding) in bits for egress features is: "
             << total_size;
  if (total_size == 0) {
    return "";
  }
  if (total_size > egress_bit_limit) {
    PS_VLOG(5) << "Total size required for egress payload serialization ("
               << total_size << ")"
               << " is greater than the system limit: " << egress_bit_limit;
    return "";
  }
  // Round up to the nearest byte, if needed.
  if (total_size % 8 != 0) {
    PS_VLOG(5) << "Rounding up the total size to nearest byte";
    total_size = (total_size / 8 + 1) * 8;
  }
  PS_VLOG(5) << "Total size (including any padding) in bits for egress "
             << "features is: " << total_size;
  std::vector<uint8_t> bytes_array(total_size / 8, 0);
  int bytes_array_idx = bytes_array.size() - 1;
  int feat_idx = 0;
  int bit_position = 0;
  for (const auto& egress_feature : egress_features) {
    PS_VLOG(5) << "Serializing feature at index: " << feat_idx;
    // TODO: Check if this returns NotFound and use a default value instead.
    PS_ASSIGN_OR_RETURN(auto serialized_feature, egress_feature->Serialize());
    PS_VLOG(5) << "Successfully serialized feature at index: " << feat_idx
               << " to: " << DebugString(serialized_feature);
    // Copy the serialized feature to the final byte array.
    for (auto feat_val_it = serialized_feature.rbegin();
         feat_val_it != serialized_feature.rend(); ++feat_val_it) {
      if (bit_position > 0 && bit_position % 8 == 0) {
        PS_VLOG(10) << "Resetting the bit position and advancing to next byte";
        bit_position = 0;
        --bytes_array_idx;
      }
      if (*feat_val_it) {
        bytes_array[bytes_array_idx] |= (*feat_val_it << bit_position);
      }
      ++bit_position;
      PS_VLOG(10) << "New bit position:, " << bit_position
                  << " byte array idx: " << bytes_array_idx;
    }
    PS_VLOG(5) << "Successfully copied feature at index: " << feat_idx
               << ", new overall vector: " << DebugString(bytes_array);
    ++feat_idx;
  }
  bytes_array.emplace_back(GetEgressVectorHeader(schema_version));
  std::string bytes_string(bytes_array.begin(), bytes_array.end());
  std::string base64_encoded_string;
  absl::Base64Escape(bytes_string, &base64_encoded_string);
  PS_VLOG(5) << "Done serializing features to bit string: "
             << DebugString(bytes_array)
             << ", base64 encoded string: " << base64_encoded_string;
  return base64_encoded_string;
}

}  // namespace

ProtectedAppSignalsGenerateBidsReactor::ProtectedAppSignalsGenerateBidsReactor(
    grpc::CallbackServerContext* context, V8DispatchClient& dispatcher,
    const BiddingServiceRuntimeConfig& runtime_config,
    const GenerateProtectedAppSignalsBidsRequest* request,
    GenerateProtectedAppSignalsBidsResponse* response,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    KVAsyncClient* ad_retrieval_async_client, KVAsyncClient* kv_async_client,
    EgressSchemaCache* egress_schema_cache,
    EgressSchemaCache* limited_egress_schema_cache)
    : BaseGenerateBidsReactor<GenerateProtectedAppSignalsBidsRequest,
                              GenerateProtectedAppSignalsBidsRequest::
                                  GenerateProtectedAppSignalsBidsRawRequest,
                              GenerateProtectedAppSignalsBidsResponse,
                              GenerateProtectedAppSignalsBidsResponse::
                                  GenerateProtectedAppSignalsBidsRawResponse>(
          runtime_config, request, response, key_fetcher_manager,
          crypto_client),
      context_(context),
      dispatcher_(dispatcher),
      ad_retrieval_async_client_(ad_retrieval_async_client),
      kv_async_client_(kv_async_client),
      ad_bids_retrieval_timeout_ms_(runtime_config.ad_retrieval_timeout_ms),
      metadata_(GrpcMetadataToRequestMetadata(context->client_metadata(),
                                              kBuyerMetadataKeysMap)),
      protected_app_signals_generate_bid_version_(
          runtime_config.default_protected_app_signals_generate_bid_version),
      ad_retrieval_version_(runtime_config.default_ad_retrieval_version),
      egress_schema_cache_(egress_schema_cache),
      limited_egress_schema_cache_(limited_egress_schema_cache) {
  DCHECK(ad_retrieval_async_client_) << "Missing: KV server Async GRPC client";
  CHECK_OK([this]() {
    PS_ASSIGN_OR_RETURN(metric_context_,
                        metric::BiddingContextMap()->Remove(request_));
    if (log_context_.is_consented()) {
      metric_context_->SetConsented(raw_request_.log_context().generation_id());
    }
    return absl::OkStatus();
  }()) << "BiddingContextMap()->Get(request) should have been called";
}

absl::Status ProtectedAppSignalsGenerateBidsReactor::ValidateRomaResponse(
    const std::vector<absl::StatusOr<DispatchResponse>>& result) {
  PS_VLOG(5) << "Validating Roma response";
  if (result.size() != 1) {
    return absl::InvalidArgumentError(kUnexpectedNumberOfRomaResponses);
  }

  const auto& response = result[0];
  if (!response.ok()) {
    return response.status();
  }

  return absl::OkStatus();
}

std::unique_ptr<kv_server::v2::GetValuesRequest>
ProtectedAppSignalsGenerateBidsReactor::CreateAdsRetrievalRequest(
    const std::string& prepare_data_for_ads_retrieval_response,
    absl::optional<ProtectedAppSignalsGenerateBidsReactor::AdRenderIds>
        ad_render_ids) {
  std::vector<std::string> ad_render_ids_list;
  if (ad_render_ids.has_value()) {
    ad_render_ids_list =
        std::vector<std::string>(ad_render_ids->begin(), ad_render_ids->end());
  }
  return std::make_unique<kv_server::v2::GetValuesRequest>(
      kv_server::application_pas::BuildRetrievalRequest(
          raw_request_.log_context(), raw_request_.consented_debug_config(),
          prepare_data_for_ads_retrieval_response,
          {{kClientIp, metadata_[kClientIpKey]},
           {kAcceptLanguage, metadata_[kAcceptLanguageKey]},
           {kUserAgent, metadata_[kUserAgentKey]}},
          raw_request_.buyer_signals(), std::move(ad_render_ids_list)));
}

std::unique_ptr<kv_server::v2::GetValuesRequest>
ProtectedAppSignalsGenerateBidsReactor::CreateKVLookupRequest(
    const ProtectedAppSignalsGenerateBidsReactor::AdRenderIds& ad_render_ids) {
  std::vector<std::string> ad_render_ids_list =
      std::vector<std::string>(ad_render_ids.begin(), ad_render_ids.end());
  auto request = std::make_unique<kv_server::v2::GetValuesRequest>(
      kv_server::application_pas::BuildLookupRequest(
          raw_request_.log_context(), raw_request_.consented_debug_config(),
          std::move(ad_render_ids_list)));
  PS_VLOG(8) << __func__
             << " Created KV lookup request: " << request->DebugString();
  return request;
}

void ProtectedAppSignalsGenerateBidsReactor::CancellableFetchAds(
    const std::string& prepare_data_for_ads_retrieval_response) {
  PS_VLOG(8, log_context_) << __func__;

  grpc::ClientContext* client_context = client_contexts_.Add();

  auto status = ad_retrieval_async_client_->ExecuteInternal(
      CreateAdsRetrievalRequest(prepare_data_for_ads_retrieval_response),
      client_context,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, prepare_data_for_ads_retrieval_response](
              KVLookUpResult ad_retrieval_result,
              ResponseMetadata response_metadata) {
            if (!ad_retrieval_result.ok()) {
              PS_VLOG(kNoisyWarn, log_context_)
                  << "Ad retrieval request failed: "
                  << ad_retrieval_result.status();
              EncryptResponseAndFinish(grpc::Status(
                  grpc::INTERNAL, ad_retrieval_result.status().ToString()));
              return;
            }

            OnFetchAdsDataDone(*std::move(ad_retrieval_result),
                               prepare_data_for_ads_retrieval_response);
          },
          [this]() {
            EncryptResponseAndFinish(
                grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
          }),
      absl::Milliseconds(ad_bids_retrieval_timeout_ms_));

  if (!status.ok()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Failed to execute ad retrieval request: " << status;
    EncryptResponseAndFinish(grpc::Status(grpc::INTERNAL, status.ToString()));
  }
}

DispatchRequest
ProtectedAppSignalsGenerateBidsReactor::CreateGenerateBidsRequest(
    std::unique_ptr<kv_server::v2::GetValuesResponse> result,
    absl::string_view prepare_data_for_ads_retrieval_response) {
  std::vector<std::shared_ptr<std::string>> input(
      kNumGenerateBidsUdfArgs, std::make_shared<std::string>());
  PopulateArgInRomaRequest(result->single_partition().string_output(),
                           ArgIndex(GenerateBidsUdfArgs::kAds), input);
  PopulateArgInRomaRequest(raw_request_.auction_signals(),
                           ArgIndex(GenerateBidsUdfArgs::kAuctionSignals),
                           input);
  PopulateArgInRomaRequest(raw_request_.buyer_signals(),
                           ArgIndex(GenerateBidsUdfArgs::kBuyerSignals), input);
  PopulateArgInRomaRequest(
      prepare_data_for_ads_retrieval_response,
      ArgIndex(GenerateBidsUdfArgs::kPreProcessedDataForRetrieval), input);
  if (prepare_data_for_ads_retrieval_response.empty() &&
      IsContextualRetrievalRequest()) {
    // If prepareDataForAdRetrieval was not run then we relay the PAS as well
    // as the encoding version directly to generateBid.
    PopulateArgInRomaRequest(
        absl::StrCat(
            "\"",
            absl::BytesToHexString(
                raw_request_.protected_app_signals().app_install_signals()),
            "\""),
        ArgIndex(GenerateBidsUdfArgs::kProtectedAppSignals), input);
    PopulateArgInRomaRequest(
        absl::StrCat(raw_request_.protected_app_signals().encoding_version()),
        ArgIndex(GenerateBidsUdfArgs::kProtectedAppSignalsVersion), input);
  }
  PopulateArgInRomaRequest(
      GetFeatureFlagJson(enable_adtech_code_logging_,
                         enable_buyer_debug_url_generation_ &&
                             raw_request_.enable_debug_reporting()),
      ArgIndex(GenerateBidsUdfArgs::kFeatureFlags), input);
  DispatchRequest request = {
      .id = raw_request_.log_context().generation_id(),
      .version_string = protected_app_signals_generate_bid_version_,
      .handler_name = kDispatchHandlerFunctionNameWithCodeWrapper,
      .input = std::move(input),
      .metadata = roma_request_context_factory_.Create(),
  };
  request.tags[kTimeoutMs] = roma_timeout_ms_;
  return request;
}

absl::StatusOr<ProtectedAppSignalsAdWithBid>
ProtectedAppSignalsGenerateBidsReactor::
    ParseProtectedSignalsGenerateBidsResponse(const std::string& response) {
  PS_VLOG(8, log_context_) << __func__;
  std::string generate_bid_response;
  if (enable_adtech_code_logging_) {
    PS_ASSIGN_OR_RETURN(
        generate_bid_response,
        ParseAndGetResponseJson(enable_adtech_code_logging_, response,
                                log_context_),
        _ << "Failed to parse ProtectedAppSignalsAdWithBid JSON "
             "response from Roma");
  } else {
    generate_bid_response = response;
  }

  ProtectedAppSignalsAdWithBid bid;
  PS_RETURN_IF_ERROR(
      google::protobuf::util::JsonStringToMessage(generate_bid_response, &bid));
  return bid;
}

absl::StatusOr<std::string>
ProtectedAppSignalsGenerateBidsReactor::GetSerializedEgressPayload(
    uint32_t schema_version, absl::string_view egress_payload,
    EgressSchemaCache& egress_schema_cache, int egress_bit_limit) {
  PS_VLOG(5) << "Fetching egress schema from cache";
  PS_ASSIGN_OR_RETURN(auto egress_features,
                      egress_schema_cache.Get(schema_version));
  PS_VLOG(5) << "Fetched egress schema successfully from cache, retreiving "
                "features in egress payload: "
             << egress_payload;
  PS_ASSIGN_OR_RETURN(auto egress_values_doc, ParseJsonString(egress_payload));
  PS_VLOG(5) << "Retrieved features from egress payload, setting them on the "
                "egress features object next";
  PS_RETURN_IF_ERROR(PopulateValuesForEgressFeatures(
      std::move(egress_values_doc.Move()), egress_features));
  PS_VLOG(5) << "Succeeded in setting features from egress payload to the "
                "egress feature object, serializing the features next";
  return SerializeEgressFeatures(schema_version, egress_features,
                                 egress_bit_limit);
}

void ProtectedAppSignalsGenerateBidsReactor::PopulateSerializedEgressPayload(
    uint32_t schema_version, std::string& egress_payload_in_proto,
    EgressSchemaCache& egress_schema_cache, int egress_bit_limit) {
  PS_VLOG(5) << "Egress payload from generateBid: " << egress_payload_in_proto;
  if (egress_payload_in_proto.empty()) {
    PS_VLOG(5) << "Egress payload received from generateBid is empty, "
               << "skipping serialization";
    return;
  }

  if (auto serialized_payload =
          GetSerializedEgressPayload(schema_version, egress_payload_in_proto,
                                     egress_schema_cache, egress_bit_limit);
      serialized_payload.ok()) {
    PS_VLOG(5) << "Populating the serialized payload into proto";
    egress_payload_in_proto = *std::move(serialized_payload);
    PS_VLOG(5) << "Populated the serialized payload into proto "
                  "successfully";
  } else {
    PS_VLOG(5) << "Failed to serialize egress payload: "
               << serialized_payload.status();
    egress_payload_in_proto.clear();
  }
}

void ProtectedAppSignalsGenerateBidsReactor::OnFetchAdsDataDone(
    std::unique_ptr<kv_server::v2::GetValuesResponse> result,
    const std::string& prepare_data_for_ads_retrieval_response) {
  PS_VLOG(8, log_context_) << __func__
                           << "Ads data returned by the ad retrieval service: "
                           << result->single_partition().string_output();
  if (result->single_partition().string_output().empty()) {
    PS_VLOG(4, log_context_) << "No ads data returned by the ad retrieval OR KV"
                                " service, finishing RPC";
    EncryptResponseAndFinish(grpc::Status::OK);
    return;
  }

  dispatch_requests_.emplace_back(CreateGenerateBidsRequest(
      std::move(result), prepare_data_for_ads_retrieval_response));
  absl::Time start_js_execution_time = absl::Now();
  ExecuteRomaRequests<ProtectedAppSignalsAdWithBid>(
      dispatch_requests_, kDispatchHandlerFunctionNameWithCodeWrapper,
      [this, start_js_execution_time](const std::string& response) {
        int js_execution_time_ms =
            (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
        LogIfError(
            metric_context_
                ->LogHistogram<metric::kPASGenerateBidJSExecutionDuration>(
                    js_execution_time_ms));
        return ParseProtectedSignalsGenerateBidsResponse(response);
      },
      [this](const ProtectedAppSignalsAdWithBid& bid) {
        LogIfError(
            metric_context_->AccumulateMetric<metric::kBiddingTotalBidsCount>(
                1));
        if (absl::Status validation_status = IsValidProtectedAppSignalsBid(bid);
            !validation_status.ok()) {
          LogIfError(
              metric_context_->LogHistogram<metric::kBiddingZeroBidPercent>(
                  1.0));
          PS_VLOG(kNoisyWarn, log_context_) << validation_status.message();
        } else {
          PS_VLOG(kNoisyInfo, log_context_)
              << "Successful non-zero protected app signals bid received";
          auto* added_bid = raw_response_.add_bids();
          *added_bid = bid;
          const int limited_egress_bits =
              absl::GetFlag(FLAGS_limited_egress_bits);
          if (limited_egress_bits <= 0) {
            PS_VLOG(5) << "Allowed limited egress bits: " << limited_egress_bits
                       << ", skipping it";
            added_bid->clear_egress_payload();
          } else {
            PopulateSerializedEgressPayload(
                kDefaultEgressSchemaVersion,
                *added_bid->mutable_egress_payload(),
                *limited_egress_schema_cache_, limited_egress_bits);
          }
          if (!raw_request_.enable_unlimited_egress() ||
              !absl::GetFlag(FLAGS_enable_temporary_unlimited_egress)) {
            PS_VLOG(5) << "Either request doesn't allow unlimited egress or "
                       << "feature is disabled by the platform";
            added_bid->clear_temporary_unlimited_egress_payload();
          } else {
            PopulateSerializedEgressPayload(
                kDefaultEgressSchemaVersion,
                *added_bid->mutable_temporary_unlimited_egress_payload(),
                *egress_schema_cache_);
          }
        }
        EncryptResponseAndFinish(grpc::Status::OK);
      });
}

DispatchRequest ProtectedAppSignalsGenerateBidsReactor::
    CreatePrepareDataForAdsRetrievalRequest() {
  PS_VLOG(8, log_context_) << __func__;
  std::vector<std::shared_ptr<std::string>> input(
      kNumPrepareDataForRetrievalUdfArgs, std::make_shared<std::string>());
  PopulateArgInRomaRequest(
      absl::StrCat(
          "\"",
          absl::BytesToHexString(
              raw_request_.protected_app_signals().app_install_signals()),
          "\""),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kProtectedAppSignals), input);
  PopulateArgInRomaRequest(
      absl::StrCat(raw_request_.protected_app_signals().encoding_version()),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kProtectedAppSignalsVersion),
      input);
  PopulateArgInRomaRequest(
      raw_request_.auction_signals(),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kAuctionSignals), input);
  PopulateArgInRomaRequest(
      raw_request_.buyer_signals(),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kBuyerSignals), input);
  PopulateArgInRomaRequest(
      GetFeatureFlagJson(enable_adtech_code_logging_,
                         /*enable_debug_url_generation=*/false),
      ArgIndex(PrepareDataForRetrievalUdfArgs::kFeatureFlags), input);
  DispatchRequest request = {
      .id = raw_request_.log_context().generation_id(),
      .version_string = ad_retrieval_version_,
      .handler_name = kPrepareDataForAdRetrievalEntryFunctionName,
      .input = std::move(input),
      .metadata = roma_request_context_factory_.Create(),
  };
  if (server_common::log::PS_VLOG_IS_ON(3)) {
    for (const auto& i : request.input) {
      PS_VLOG(kDispatch, log_context_)
          << "Roma request input to prepared data for ads retrieval: " << *i;
    }
  }
  request.tags[kTimeoutMs] = roma_timeout_ms_;
  return request;
}

void ProtectedAppSignalsGenerateBidsReactor::StartNonContextualAdsRetrieval() {
  PS_VLOG(8, log_context_) << __func__;
  embeddings_requests_.emplace_back(CreatePrepareDataForAdsRetrievalRequest());
  absl::Time start_js_execution_time = absl::Now();
  ExecuteRomaRequests<std::string>(
      embeddings_requests_, kPrepareDataForAdRetrievalHandler,
      [this, start_js_execution_time](
          const std::string& response) -> absl::StatusOr<std::string> {
        int js_execution_time_ms =
            (absl::Now() - start_js_execution_time) / absl::Milliseconds(1);
        LogIfError(metric_context_->LogHistogram<
                   metric::kPASPrepareDataForRetrievalJSExecutionDuration>(
            js_execution_time_ms));
        PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                            ParseJsonString(response));
        return SerializeJsonDoc(document["response"]);
      },
      [this](const std::string& parsed_response) {
        FetchAds(parsed_response);
      });
}

bool ProtectedAppSignalsGenerateBidsReactor::IsContextualRetrievalRequest() {
  if (is_contextual_retrieval_request_.has_value()) {
    return *is_contextual_retrieval_request_;
  }

  if (!raw_request_.has_contextual_protected_app_signals_data()) {
    PS_VLOG(5, log_context_) << "No contextual PAS data found";
    is_contextual_retrieval_request_ = false;
    return false;
  }

  const auto& protected_app_signals_data =
      raw_request_.contextual_protected_app_signals_data();
  if (protected_app_signals_data.ad_render_ids().size() > 0 &&
      !protected_app_signals_data.fetch_ads_from_retrieval_service()) {
    PS_VLOG(5, log_context_)
        << "Contextual ad render ids: "
        << protected_app_signals_data.ad_render_ids().size()
        << ", fetch_ads_from_retrieval_service: "
        << protected_app_signals_data.fetch_ads_from_retrieval_service();
    is_contextual_retrieval_request_ = true;
    return true;
  }

  is_contextual_retrieval_request_ = false;
  return false;
}

void ProtectedAppSignalsGenerateBidsReactor::CancellableFetchAdsMetadata(
    const std::string& prepare_data_for_ads_retrieval_response) {
  const auto& ad_render_ids =
      raw_request_.contextual_protected_app_signals_data().ad_render_ids();
  PS_VLOG(8, log_context_) << __func__ << " Found ad render ids: "
                           << absl::StrJoin(
                                  raw_request_
                                      .contextual_protected_app_signals_data()
                                      .ad_render_ids(),
                                  ", ");

  grpc::ClientContext* client_context = client_contexts_.Add();

  auto status = kv_async_client_->ExecuteInternal(
      CreateKVLookupRequest(ad_render_ids), client_context,
      CancellationWrapper(
          context_, enable_cancellation_,
          [this, prepare_data_for_ads_retrieval_response](
              KVLookUpResult kv_look_up_result,
              ResponseMetadata response_metadata) {
            PS_VLOG(8) << "On KV response";
            if (!kv_look_up_result.ok()) {
              PS_VLOG(kNoisyWarn, log_context_)
                  << "KV metadata request failed: "
                  << kv_look_up_result.status();
              EncryptResponseAndFinish(grpc::Status(
                  grpc::INTERNAL, kv_look_up_result.status().ToString()));
              return;
            }

            OnFetchAdsDataDone(*std::move(kv_look_up_result),
                               prepare_data_for_ads_retrieval_response);
          },
          [this]() {
            EncryptResponseAndFinish(
                grpc::Status(grpc::CANCELLED, kRequestCancelled));
          }),
      absl::Milliseconds(ad_bids_retrieval_timeout_ms_));

  if (!status.ok()) {
    PS_VLOG(kNoisyWarn, log_context_)
        << "Failed to execute ads metadata KV lookup request: " << status;
    EncryptResponseAndFinish(grpc::Status(grpc::INTERNAL, status.ToString()));
  }
}

void ProtectedAppSignalsGenerateBidsReactor::StartContextualAdsRetrieval() {
  PS_VLOG(8, log_context_) << __func__;
  std::string prepare_data_for_ads_retrieval_response;
  FetchAdsMetadata(prepare_data_for_ads_retrieval_response);
}

void ProtectedAppSignalsGenerateBidsReactor::Execute() {
  if (enable_cancellation_ && context_->IsCancelled()) {
    EncryptResponseAndFinish(
        grpc::Status(grpc::StatusCode::CANCELLED, kRequestCancelled));
    return;
  }

  PS_VLOG(8, log_context_) << __func__;
  PS_VLOG(kEncrypted, log_context_)
      << "GenerateBidsRequest exported in EventMessage";
  log_context_.SetEventMessageField(*request_);
  PS_VLOG(kPlain, log_context_)
      << "GenerateBidsRawRequest exported in EventMessage";
  log_context_.SetEventMessageField(raw_request_);

  if (IsContextualRetrievalRequest()) {
    StartContextualAdsRetrieval();
  } else {
    // Trigger the request processing workflow to:
    // 1. Fetch protected embeddings for retrieval
    // 2. Fetch top-k ads and metadata using the embeddings retrieved in 1.
    // 3. Run the `generateBid` UDF for Protected App Signals and return the
    //    response back to BFE.
    StartNonContextualAdsRetrieval();
  }
}

void ProtectedAppSignalsGenerateBidsReactor::OnDone() { delete this; }

void ProtectedAppSignalsGenerateBidsReactor::EncryptResponseAndFinish(
    grpc::Status status) {
  PS_VLOG(8, log_context_) << __func__;
  PS_VLOG(kPlain, log_context_)
      << "GenerateProtectedAppSignalsBidsRawResponse exported in EventMessage";
  log_context_.SetEventMessageField(raw_response_);
  // ExportEventMessage before encrypt response
  log_context_.ExportEventMessage(/*if_export_consented=*/true);
  if (!EncryptResponse()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to encrypt the generate app signals bids response.";
    status = grpc::Status(grpc::INTERNAL, kInternalServerError);
  }
  Finish(status);
}

}  // namespace privacy_sandbox::bidding_auction_servers
