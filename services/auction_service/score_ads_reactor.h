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

#ifndef SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_H_
#define SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_H_

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/loggers/request_context_impl.h"
#include "services/common/metric/server_definition.h"
#include "services/common/reporters/async_reporter.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kNoAdsToScore[] = "No ads to score.";
inline constexpr char kNoAdsWithValidScoringSignals[] =
    "No ads with valid scoring signals.";
inline constexpr char kNoTrustedScoringSignals[] =
    "Empty trusted scoring signals";

// This is a gRPC reactor that serves a single ScoreAdsRequest.
// It stores state relevant to the request and after the
// response is finished being served, ScoreAdsReactor cleans up all
// necessary state and grpc releases the reactor from memory.
class ScoreAdsReactor
    : public CodeDispatchReactor<
          ScoreAdsRequest, ScoreAdsRequest::ScoreAdsRawRequest,
          ScoreAdsResponse, ScoreAdsResponse::ScoreAdsRawResponse> {
 public:
  explicit ScoreAdsReactor(
      const CodeDispatchClient& dispatcher, const ScoreAdsRequest* request,
      ScoreAdsResponse* response,
      std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const AsyncReporter* async_reporter,
      const AuctionServiceRuntimeConfig& runtime_config);

  // Initiates the asynchronous execution of the ScoreAdsRequest.
  virtual void Execute();

 private:
  // Asynchronous callback used by the v8 code executor to return a result. This
  // will be called in a different thread owned by the code dispatch library.
  //
  // output: a status or DispatchResponse representing the result of the code
  // dispatch execution.
  // ad: the ad and bid that was scored.
  void ScoreAdsCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& output);

  log::ContextImpl::ContextMap GetLoggingContext(
      const ScoreAdsRequest::ScoreAdsRawRequest& score_ads_request);

  // Performs debug reporting for all scored ads by the seller.
  void PerformDebugReporting(
      const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score);

  void PerformReporting(const ScoreAdsResponse::AdScore& winning_ad_score);
  // Finishes the RPC call with an OK status.
  void FinishWithOkStatus();
  void ReportingCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);

  // The key is the id of the DispatchRequest, and the value is the ad
  // used to create the dispatch request. This map is used to amend each ad's
  // DispatchResponse with more data which is then passed into the final
  // ScoreAdsResponse.
  absl::flat_hash_map<
      std::string,
      std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>>
      ad_data_;
  std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger_;
  const AsyncReporter& async_reporter_;
  bool enable_seller_debug_url_generation_;
  std::string roma_timeout_ms_;
  log::ContextImpl log_context_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::AuctionContext> metric_context_;

  std::vector<std::unique_ptr<ScoreAdsResponse::AdScore>> ad_scores_;

  // Flags needed to be passed as input to the code which wraps AdTech provided
  // code.
  bool enable_seller_code_wrapper_;
  bool enable_adtech_code_logging_;
  bool enable_report_result_url_generation_;
  bool enable_report_win_url_generation_;
  std::string seller_origin_;
  int max_allowed_size_debug_url_chars_;
  long max_allowed_size_all_debug_urls_chars_;
};
}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_H_
