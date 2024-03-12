/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_BIDDING_SERVICE_PROTECTED_APP_SIGNALS_GENERATE_BIDS_REACTOR_H_
#define SERVICES_BIDDING_SERVICE_PROTECTED_APP_SIGNALS_GENERATE_BIDS_REACTOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "public/query/v2/get_values_v2.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/common/clients/kv_server/kv_async_client.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"

namespace privacy_sandbox::bidding_auction_servers {

class ProtectedAppSignalsGenerateBidsReactor
    : public BaseGenerateBidsReactor<
          GenerateProtectedAppSignalsBidsRequest,
          GenerateProtectedAppSignalsBidsRequest::
              GenerateProtectedAppSignalsBidsRawRequest,
          GenerateProtectedAppSignalsBidsResponse,
          GenerateProtectedAppSignalsBidsResponse::
              GenerateProtectedAppSignalsBidsRawResponse> {
 public:
  explicit ProtectedAppSignalsGenerateBidsReactor(
      const grpc::CallbackServerContext* context,
      CodeDispatchClient& dispatcher,
      const BiddingServiceRuntimeConfig& runtime_config,
      const GenerateProtectedAppSignalsBidsRequest* request,
      GenerateProtectedAppSignalsBidsResponse* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      KVAsyncClient* ad_retrieval_async_client, KVAsyncClient* kv_async_client);

  virtual ~ProtectedAppSignalsGenerateBidsReactor() = default;

  // ProtectedAppSignalsGenerateBidsReactor is neither copyable nor movable.
  ProtectedAppSignalsGenerateBidsReactor(
      const ProtectedAppSignalsGenerateBidsReactor&) = delete;
  ProtectedAppSignalsGenerateBidsReactor& operator=(
      const ProtectedAppSignalsGenerateBidsReactor&) = delete;

  // Executes the workflow required to handle the incoming GRPC request.
  void Execute() override;

 private:
  void OnDone() override;
  void OnCancel() override;

  DispatchRequest CreatePrepareDataForAdsRetrievalRequest();

  bool IsContextualRetrievalRequest();
  void StartContextualAdsRetrieval();
  void StartNonContextualAdsRetrieval();

  using AdRenderIds = google::protobuf::RepeatedPtrField<std::string>;
  std::unique_ptr<kv_server::v2::GetValuesRequest>
  CreateAdsRetrievalOrKVLookupRequest(
      const std::string& prepare_data_for_ads_retrieval_response,
      absl::optional<AdRenderIds> ad_render_ids = absl::nullopt);

  void FetchAds(const std::string& prepare_data_for_ads_retrieval_response);
  void FetchAdsMetadata(
      const std::string& prepare_data_for_ads_retrieval_response);

  DispatchRequest CreateGenerateBidsRequest(
      std::unique_ptr<kv_server::v2::GetValuesResponse> result,
      absl::string_view prepare_data_for_ads_retrieval_response);

  void OnFetchAdsDataDone(
      std::unique_ptr<kv_server::v2::GetValuesResponse> result,
      const std::string& prepare_data_for_ads_retrieval_response);

  void EncryptResponseAndFinish(grpc::Status status);

  absl::Status ValidateRomaResponse(
      const std::vector<absl::StatusOr<DispatchResponse>>& result);

  absl::StatusOr<ProtectedAppSignalsAdWithBid>
  ParseProtectedSignalsGenerateBidsResponse(const std::string& response);

  template <typename T>
  void ExecuteRomaRequests(
      std::vector<DispatchRequest>& requests,
      absl::string_view roma_entry_function,
      std::function<absl::StatusOr<T>(const std::string&)> parse_response,
      std::function<void(const T&)> on_successful_response) {
    PS_VLOG(8, log_context_) << __func__;
    auto status = dispatcher_.BatchExecute(
        requests,
        [this, roma_entry_function, parse_response = std::move(parse_response),
         on_successful_response = std::move(on_successful_response), requests](
            const std::vector<absl::StatusOr<DispatchResponse>>& result) {
          if (auto status = ValidateRomaResponse(result); !status.ok()) {
            PS_VLOG(2, log_context_)
                << "Failed to run UDF: " << roma_entry_function
                << ". Error: " << status;
            EncryptResponseAndFinish(
                grpc::Status(grpc::StatusCode::INTERNAL, status.ToString()));
            return;
          }

          PS_VLOG(3, log_context_) << "Response from " << roma_entry_function
                                   << ": " << result[0]->resp;
          auto parsed_response = std::move(parse_response)(result[0]->resp);
          if (!parsed_response.ok()) {
            PS_VLOG(2, log_context_)
                << "Failed to parse the response from: " << roma_entry_function
                << ". Error: " << parsed_response.status();
            EncryptResponseAndFinish(
                grpc::Status(grpc::StatusCode::INTERNAL,
                             parsed_response.status().ToString()));
            return;
          }

          PS_VLOG(3, log_context_)
              << "Successful V8 Response from: " << roma_entry_function;

          std::move(on_successful_response)(*std::move(parsed_response));
        });

    if (!status.ok()) {
      PS_VLOG(2, log_context_) << "Failed to execute " << roma_entry_function
                               << " in Roma. Error: " << status.ToString();
      EncryptResponseAndFinish(
          grpc::Status(grpc::StatusCode::INTERNAL, status.ToString()));
    }
  }

  KVAsyncClient* ad_retrieval_async_client_;
  KVAsyncClient* kv_async_client_;
  int ad_bids_retrieval_timeout_ms_;
  RequestMetadata metadata_;
  std::vector<DispatchRequest> embeddings_requests_;
  absl::optional<bool> is_contextual_retrieval_request_;

  // UDF versions to use for this request.
  const std::string& protected_app_signals_generate_bid_version_;
  const std::string& ad_retrieval_version_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_PROTECTED_APP_SIGNALS_GENERATE_BIDS_REACTOR_H_
