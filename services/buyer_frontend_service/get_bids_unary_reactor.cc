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

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "glog/logging.h"
#include "services/buyer_frontend_service/util/proto_factory.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/loggers/build_input_process_response_benchmarking_logger.h"
#include "services/common/loggers/no_ops_logger.h"
#include "services/common/util/request_metadata.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse;

bool GetBidsUnaryReactor::DecryptRequest() {
  if (request_->key_id().empty()) {
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kEmptyKeyIdError));
    return false;
  }

  if (request_->request_ciphertext().empty()) {
    Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        kEmptyCiphertextError));
    return false;
  }

  std::optional<server_common::PrivateKey> private_key =
      key_fetcher_manager_->GetPrivateKey(request_->key_id());
  if (!private_key.has_value()) {
    Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kInvalidKeyIdError));
    return false;
  }

  absl::StatusOr<HpkeDecryptResponse> decrypt_response =
      crypto_client_->HpkeDecrypt(*private_key, request_->request_ciphertext());
  if (!decrypt_response.ok()) {
    Finish(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kMalformedCiphertext));
    return false;
  }

  hpke_secret_ = std::move(decrypt_response->secret());
  return raw_request_.ParseFromString(decrypt_response->payload());
}

void GetBidsUnaryReactor::Execute() {
  benchmarking_logger_->Begin();
  if (config_.encryption_enabled && !DecryptRequest()) {
    return;
  }

  BiddingSignalsRequest bidding_signals_request(raw_request_,
                                                this->kv_metadata_);
  // Get Bidding Signals.
  bidding_signals_async_provider_->Get(
      bidding_signals_request,
      [reactor{this},
       this](absl::StatusOr<std::unique_ptr<BiddingSignals>> response) {
        if (!response.ok()) {
          // Return error to client.
          logger_.vlog(1, "GetBiddingSignals request failed with status:",
                       response.status());
          reactor->Finish(grpc::Status(
              static_cast<grpc::StatusCode>(response.status().code()),
              std::string(response.status().message())));
          return;
        }
        // Final callback needs to check status of others and send bidding
        // request.
        reactor->PrepareAndGenerateBid(std::move(response.value()));
      },
      absl::Milliseconds(config_.bidding_signals_load_timeout_ms));
}

// Process Outputs from Actions to prepare bidding request.
// All Preload actions must have completed before this is invoked.
void GetBidsUnaryReactor::PrepareAndGenerateBid(
    std::unique_ptr<BiddingSignals> bidding_signals) {
  const auto& log_context = raw_request_.log_context();
  std::unique_ptr<GenerateBidsRequest> bidding_input =
      ProtoFactory::CreateGenerateBidsRequest(
          raw_request_, raw_request_.buyer_input(), std::move(bidding_signals),
          log_context);

  logger_.vlog(2, "GenerateBidsRequest:\n", bidding_input->DebugString());

  absl::Status execute_result = this->bidding_async_client_->Execute(
      std::move(bidding_input), {},
      [reactor{this},
       this](absl::StatusOr<std::unique_ptr<GenerateBidsResponse>> response) {
        if (!response.ok()) {
          // Return error to client.
          logger_.vlog(1,
                       "Execution of GenerateBids request failed with status: ",
                       response.status());
          reactor->benchmarking_logger_->End();
          reactor->Finish(grpc::Status(
              static_cast<grpc::StatusCode>(response.status().code()),
              std::string(response.status().message())));
          return;
        }
        // Parse and convert response.
        reactor->get_bids_response_->set_allocated_raw_response(
            ProtoFactory::CreateGetBidsRawResponse(std::move(response.value()))
                .release());

        logger_.vlog(2, "GetBidsResponse:\n",
                     reactor->get_bids_response_->DebugString());

        if (reactor->config_.encryption_enabled &&
            !reactor->EncryptResponse()) {
          return;
        }

        reactor->benchmarking_logger_->End();
        reactor->Finish(grpc::Status::OK);
      },
      absl::Milliseconds(config_.generate_bid_timeout_ms));
  if (!execute_result.ok()) {
    logger_.error(
        absl::StrFormat("Failed to make async GenerateBids call: (error: %s)",
                        execute_result.ToString()));
    Finish(grpc::Status(grpc::INTERNAL, kInternalServerError));
  }
}

bool GetBidsUnaryReactor::EncryptResponse() {
  std::string payload = get_bids_response_->raw_response().SerializeAsString();
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
      aead_encrypt = crypto_client_->AeadEncrypt(payload, hpke_secret_);
  if (!aead_encrypt.ok()) {
    Finish(grpc::Status(grpc::StatusCode::INTERNAL, ""));
    return false;
  }

  get_bids_response_->set_response_ciphertext(
      aead_encrypt->encrypted_data().ciphertext());
  get_bids_response_->clear_raw_response();
  return true;
}

ContextLogger::ContextMap GetBidsUnaryReactor::GetLoggingContext() {
  const auto& log_context = raw_request_.log_context();
  return {{kGenerationId, log_context.generation_id()},
          {kBuyerDebugId, log_context.adtech_debug_id()}};
}

GetBidsUnaryReactor::GetBidsUnaryReactor(
    grpc::CallbackServerContext& context,
    const GetBidsRequest& get_bids_request, GetBidsResponse& get_bids_response,
    const BiddingSignalsAsyncProvider& bidding_signals_async_provider,
    const BiddingAsyncClient& bidding_async_client, const GetBidsConfig& config,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client, bool enable_benchmarking)
    : context_(&context),
      request_(&get_bids_request),
      raw_request_(get_bids_request.raw_request()),
      get_bids_response_(&get_bids_response),
      // TODO(b/278039901): Add integration test for metadata forwarding.
      kv_metadata_(GrpcMetadataToRequestMetadata(context.client_metadata(),
                                                 kBuyerKVMetadata)),
      bidding_signals_async_provider_(&bidding_signals_async_provider),
      bidding_async_client_(&bidding_async_client),
      config_(config),
      key_fetcher_manager_(key_fetcher_manager),
      crypto_client_(crypto_client),
      logger_(GetLoggingContext()) {
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
    return absl::OkStatus();
  }()) << "BfeContextMap()->Get(request) should have been called";
}

// Deletes all data related to this object.
void GetBidsUnaryReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
