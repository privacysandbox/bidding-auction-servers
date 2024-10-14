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
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include <google/protobuf/text_format.h>
#include <rapidjson/stringbuffer.h>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/server_definition.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/util/cancellation_wrapper.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kDeviceComponentAuctionWithPAS[] =
    "Protected App Signals Auction Input cannot be considered for "
    "Device Component Auction";
inline constexpr char kNoAdsWithValidScoringSignals[] =
    "No ads with valid scoring signals.";
inline constexpr char kNoValidComponentAuctions[] =
    "No component auction results in request.";
// An aggregate of the data we track when scoring all the ads.
struct ScoringData {
  // Index of the most desirable ad. This helps us to set the overall response
  // object just once.
  int index_of_most_desirable_ad = 0;
  // Count of rejected bids.
  int seller_rejected_bid_count = 0;
  // Map of all the desirability/scores and corresponding scored ad's index in
  // the response from the scoreAd's UDF.
  absl::flat_hash_map<float, std::list<int>> score_ad_map;
  // Saving the desirability allows us to compare desirability between ads
  // without re-parsing the current most-desirable ad every time.
  float desirability_of_most_desirable_ad = 0;
  // List of rejection reasons provided by seller.
  std::vector<ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reasons;
  // The most desirable ad.
  std::optional<ScoreAdsResponse::AdScore> winning_ad;

  void UpdateWinner(int index, const ScoreAdsResponse::AdScore& ad_score);
};

// Fields needed from component level AuctionResult for
// reporting in the Top level Seller
struct ComponentReportingDataInAuctionResult {
  std::string component_seller;
  WinReportingUrls win_reporting_urls;
  std::string buyer_reporting_id;
};

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
      grpc::CallbackServerContext* context, V8DispatchClient& dispatcher,
      const ScoreAdsRequest* request, ScoreAdsResponse* response,
      std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const AsyncReporter* async_reporter,
      const AuctionServiceRuntimeConfig& runtime_config);

  // Initiates the asynchronous execution of the ScoreAdsRequest.
  void Execute() override;

 private:
  using AdWithBidMetadata =
      ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
  using ProtectedAppSignalsAdWithBidMetadata =
      ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
  // Finds the ad type of the scored ad and set it. After the function call,
  // expect one of the input pointers to be populated.
  void FindScoredAdType(absl::string_view response_id,
                        AdWithBidMetadata** ad_with_bid_metadata,
                        ProtectedAppSignalsAdWithBidMetadata**
                            protected_app_signals_ad_with_bid_metadata);
  // Populates the ad render URL and other ad type specific data in the ad score
  // response to be sent back to SFE.
  void PopulateRelevantFieldsInResponse(int index_of_most_desirable_ad,
                                        absl::string_view request_id,
                                        ScoreAdsResponse::AdScore& winning_ad);
  // Finds the winning ad (if one exists) among the responses returned by Roma.
  // Returns all the data associated with scoring that can then be later used
  // for finding second highest bid (among other things).
  ScoringData FindWinningAd(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);

  // Populates the data about the highest second other bid in the response to
  // be returned to SFE.
  void PopulateHighestScoringOtherBidsData(
      int index_of_most_desirable_ad,
      const absl::flat_hash_map<float, std::list<int>>& score_ad_map,
      const std::vector<absl::StatusOr<DispatchResponse>>& responses,
      ScoreAdsResponse::AdScore& winning_ad);

  // Asynchronous callback used by the v8 code executor to return a result. This
  // will be called in a different thread owned by the code dispatch library.
  //
  // output: a status or DispatchResponse representing the result of the code
  // dispatch execution.
  // ad: the ad and bid that was scored.
  void ScoreAdsCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& output,
      bool enable_debug_reporting);

  absl::btree_map<std::string, std::string> GetLoggingContext(
      const ScoreAdsRequest::ScoreAdsRawRequest& score_ads_request);

  // Performs debug reporting for all scored ads by the seller.
  void PerformDebugReporting(
      const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score);

  static constexpr char kRomaTimeoutMs[] = "TimeoutMs";

  void DispatchReportingRequestForPA(
      absl::string_view dispatch_id,
      const ScoreAdsResponse::AdScore& winning_ad_score,
      const std::shared_ptr<std::string>& auction_config,
      const BuyerReportingMetadata& buyer_reporting_metadata);

  void DispatchReportingRequestForPAS(
      const ScoreAdsResponse::AdScore& winning_ad_score,
      const std::shared_ptr<std::string>& auction_config,
      const BuyerReportingMetadata& buyer_reporting_metadata,
      std::string_view egress_payload,
      absl::string_view temporary_egress_payload);

  void DispatchReportingRequest(
      const ReportingDispatchRequestData& dispatch_request_data);
  [[deprecated(
      "DEPRECATED for Protected Audience. Please use "
      "PerformReportingWithSellerAndBuyerCodeIsolation instead")]]
  void PerformReporting(const ScoreAdsResponse::AdScore& winning_ad_score,
                        absl::string_view id);

  // Initializes buyer_reporting_dispatch_request_data_ with the
  // fields included in winning_ad_score.
  void InitializeBuyerReportingDispatchRequestData(
      const ScoreAdsResponse::AdScore& winning_ad_score);

  // Performs reportResult and reportWin udf execution with seller and buyer
  // code isolation
  void PerformReportingWithSellerAndBuyerCodeIsolation(
      const ScoreAdsResponse::AdScore& winning_ad_score, absl::string_view id);

  // Dispatches request to reportResult() udf with seller and buyer udf
  // isolation.
  // These global objects are expected to be set before
  // DispatchReportResultRequest call:
  // - buyer_reporting_dispatch_request_data_
  // - post_auction_signals_
  // - reporting_dispatch_request_config_
  // - raw_response_
  void DispatchReportResultRequest(
      const ScoreAdsResponse::AdScore& winning_ad_score);
  // Dispatches request to reportResult() udf with seller and buyer udf
  // isolation for component auctions.
  // These global objects are expected to be set before
  // DispatchReportResultRequest call:
  // - buyer_reporting_dispatch_request_data_
  // - post_auction_signals_
  // - reporting_dispatch_request_config_
  // - raw_response_
  void DispatchReportResultRequestForComponentAuction(
      const ScoreAdsResponse::AdScore& winning_ad_score);
  // Dispatches request to reportResult() udf with seller and buyer udf
  // isolation for top level auctions.
  void DispatchReportResultRequestForTopLevelAuction(
      const ScoreAdsResponse::AdScore& winning_ad_score,
      absl::string_view dispatch_id);

  // Publishes metrics and Finishes the RPC call with a status.
  void FinishWithStatus(const grpc::Status& status);

  // Encrypt response and Finishes the RPC call with an OK status.
  void EncryptAndFinishOK();

  void ReportingCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);

  // Function called after reportResult udf execution.
  void CancellableReportResultCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);
  void ReportWinCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);
  // Creates and populates dispatch requests using AdWithBidMetadata objects
  // in the input proto for single seller and component auctions.
  void PopulateProtectedAudienceDispatchRequests(
      bool enable_debug_reporting,
      const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
          scoring_signals,
      const std::shared_ptr<std::string>& auction_config,
      google::protobuf::RepeatedPtrField<AdWithBidMetadata>& ads);

  // Creates and populates dispatch requests using ComponentAuctionResult
  // objects in the input proto for top-level auctions.
  absl::Status PopulateTopLevelAuctionDispatchRequests(
      bool enable_debug_reporting,
      const std::shared_ptr<std::string>& auction_config,
      google::protobuf::RepeatedPtrField<AuctionResult>&
          component_auction_results);

  // Creates and populates dispatch requests using
  // ProtectedAppSignalsAdWithBidMetadata objects
  // in the input proto for single seller auctions auctions
  // if the feature flag is enabled.
  void MayPopulateProtectedAppSignalsDispatchRequests(
      bool enable_debug_reporting,
      const absl::flat_hash_map<std::string, rapidjson::StringBuffer>&
          scoring_signals,
      const std::shared_ptr<std::string>& auction_config,
      google::protobuf::RepeatedPtrField<ProtectedAppSignalsAdWithBidMetadata>&
          protected_app_signals_ad_bids);

  // Sets the required fields in the passed AdScore object and populates
  // scoring data.
  // The AdScore fields that need to be parsed from ROMA response
  // must be populated separately before this is called.
  void HandleScoredAd(int index, float buyer_bid,
                      absl::string_view ad_with_bid_currency,
                      absl::string_view interest_group_name,
                      absl::string_view interest_group_owner,
                      absl::string_view interest_group_origin,
                      const rapidjson::Document& response_json, AdType ad_type,
                      ScoreAdsResponse::AdScore& score_ads_response,
                      ScoringData& scoring_data,
                      const std::string& dispatch_response_id);

  CLASS_CANCELLATION_WRAPPER(ReportResultCallback, enable_cancellation_,
                             context_, FinishWithStatus)

  grpc::CallbackServerContext* context_;

  // Dispatches execution requests to a library that runs V8 workers in
  // separate processes.
  V8DispatchClient& dispatcher_;
  std::vector<DispatchRequest> dispatch_requests_;

  // The key is the id of the DispatchRequest, and the value is the ad
  // used to create the dispatch request. This map is used to amend each ad's
  // DispatchResponse with more data which is then passed into the final
  // ScoreAdsResponse.
  absl::flat_hash_map<
      std::string,
      std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>>
      ad_data_;

  // Map of dispatch id to component level reporting data in a component auction
  // result.
  absl::flat_hash_map<std::string, ComponentReportingDataInAuctionResult>
      component_level_reporting_data_;

  absl::flat_hash_map<std::string,
                      std::unique_ptr<ProtectedAppSignalsAdWithBidMetadata>>
      protected_app_signals_ad_data_;
  std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger_;
  const AsyncReporter& async_reporter_;
  bool enable_seller_debug_url_generation_;
  std::string roma_timeout_ms_;
  RequestLogContext log_context_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::AuctionContext> metric_context_;

  // Used for debug reporting. Keyed on Roma dispatch ID.
  absl::flat_hash_map<std::string, std::unique_ptr<ScoreAdsResponse::AdScore>>
      ad_scores_;

  // Flags needed to be passed as input to the code which wraps AdTech provided
  // code.
  bool enable_adtech_code_logging_;
  bool enable_report_result_url_generation_;
  const bool enable_protected_app_signals_;
  bool enable_report_win_input_noising_;
  std::string seller_origin_;
  int max_allowed_size_debug_url_chars_;
  long max_allowed_size_all_debug_urls_chars_;
  BuyerReportingDispatchRequestData buyer_reporting_dispatch_request_data_ = {
      .log_context = log_context_};
  rapidjson::Document seller_device_signals_;
  // Specifies whether this is a single seller or component auction.
  // Impacts the creation of scoreAd input params and
  // parsing of scoreAd output.
  AuctionScope auction_scope_;
  // Specifies which verison of scoreAd to use for this request.
  absl::string_view code_version_;
  bool enable_report_win_url_generation_;
  bool enable_seller_and_buyer_code_isolation_;
  ReportingDispatchRequestConfig reporting_dispatch_request_config_;
  PostAuctionSignals post_auction_signals_;
  google::protobuf::RepeatedPtrField<std::string>
  GetEmptyAdComponentRenderUrls() {
    static google::protobuf::RepeatedPtrField<std::string>
        empty_ad_component_render_urls;
    return empty_ad_component_render_urls;
  }
};
}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_H_
