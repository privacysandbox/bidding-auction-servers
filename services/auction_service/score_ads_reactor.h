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

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/server_definition.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/util/cancellation_wrapper.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kNoAdsWithValidScoringSignals[] =
    "No ads with valid scoring signals.";
inline constexpr char kNoValidComponentAuctions[] =
    "No component auction results in request.";
// An aggregate of the data we track when scoring all the ads.
struct ScoringData {
  // Index of the most desirable ad. This helps us to set the overall response
  // object just once.
  int winner_index = -1;
  // Count of rejected bids.
  int seller_rejected_bid_count = 0;
  // Map of all the desirability/scores and corresponding scored ad's index in
  // the response from the scoreAd's UDF.
  absl::flat_hash_map<float, std::list<int>> score_ad_map;
  // List of rejection reasons provided by seller.
  google::protobuf::RepeatedPtrField<
      ScoreAdsResponse::AdScore::AdRejectionReason>
      ad_rejection_reasons;
  // The scored bids are sorted in descending order first. The highest scored
  // ad(s) are added to this list. Note that there can be multiple highest
  // scored ads that can have the same score -- in such a case the winner is
  // chosen randomly among these candidates to ensure fairness.
  std::vector<int> winner_cand_indices;
  // Indices of ghost winner candidates. Currently, we populate ghost winner
  // candidates that have the highest and equal scores. If multiple such
  // candidates are present, and we have to choose a subset from them then
  // we choose a random sample from these to ensure fairness.
  std::vector<int> ghost_winner_cand_indices;
  // Indices of ghost winners.
  std::vector<int> ghost_winner_indices;

  void ChooseWinnerAndGhostWinners(size_t max_ghost_winners);
};

// Fields needed from component level AuctionResult for
// reporting in the Top level Seller
struct ComponentReportingDataInAuctionResult {
  std::string component_seller;
  WinReportingUrls win_reporting_urls;
  std::string buyer_reporting_id;
  std::string buyer_and_seller_reporting_id;
};

// Group of data associated with the ad scored via dispatch to Roma.
struct ScoredAdData {
  // Response returned from Roma.
  ScoreAdsResponse::AdScore ad_score;

  // Parsed JSON response object.
  rapidjson::Document response_json;

  // Populated to a non-null value only if the scored ad was of type Protected
  // Audience. Note: Underlying object is stored in `ad_data_`
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata*
      protected_audience_ad_with_bid = nullptr;

  // Populated to a non-null value only if the scored ad was of type Protected
  // App Signals. Note: Underlying object is stored in
  // `protected_app_signals_ad_data_`
  ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata*
      protected_app_signals_ad_with_bid = nullptr;

  // ID of the request/response to/from Roma.
  absl::string_view id;

  // K-anon status of the ad (derived from incoming AdWithBidMetadata)
  bool k_anon_status = false;

  // Swaps the data memberwise.
  void Swap(ScoredAdData& other);

  // Compares based on score, bid and k-anon status.
  bool operator>(const ScoredAdData& other) const;
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
      const AsyncReporter& async_reporter,
      const AuctionServiceRuntimeConfig& runtime_config);

  // Initiates the asynchronous execution of the ScoreAdsRequest.
  void Execute() override;

 private:
  using AdWithBidMetadata =
      ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
  using ProtectedAppSignalsAdWithBidMetadata =
      ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
  using OptionalAdRejectionReason =
      std::optional<ScoreAdsResponse::AdScore::AdRejectionReason>;
  // Finds the ad type of the scored ad and set it. After the function call,
  // expect one of the input pointers to be populated.
  void FindScoredAdType(absl::string_view response_id,
                        AdWithBidMetadata** ad_with_bid_metadata,
                        ProtectedAppSignalsAdWithBidMetadata**
                            protected_app_signals_ad_with_bid_metadata);
  // Populates the ad render URL and other ad type specific data in the ad score
  // response to be sent back to SFE.
  void PopulateRelevantFieldsInResponse(int index,
                                        std::vector<ScoredAdData>& parsed_ads);

  // Adds the k-anon join candidate to the scored winner/ghost winners. This
  // is needed for top level server orchestrated auctio
  void MayAddKAnonJoinCandidate(absl::string_view request_id,
                                ScoreAdsResponse::AdScore& ad_score);

  // Filters out invalid Roma responses while updating the corresponding error
  // metrics.
  //
  // Returns valid responses.
  std::vector<ScoredAdData> CollectValidRomaResponses(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);

  // Finds the winning ad (if one exists) among the responses returned by Roma.
  // Returns all the data associated with scoring that can then be later used
  // for finding second highest bid (among other things).
  ScoringData FindWinningAd(std::vector<ScoredAdData>& parsed_ads);

  // Populates the data about the highest second other bid in the response to
  // be returned to SFE.
  void PopulateHighestScoringOtherBidsData(
      int index_of_most_desirable_ad,
      const absl::flat_hash_map<float, std::list<int>>& score_ad_map,
      const std::vector<ScoredAdData>& responses,
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

  // Performs debug reporting for all scored ads by the seller, except for the
  // winning ad in component auctions.
  // These objects are expected to be set before the function call:
  // - ad_scores_
  // - post_auction_signals_
  void PerformDebugReporting();

  // Populates the debug report urls in the response, only for the winning
  // interest group in component auctions, when debug reporting is enabled.
  void PopulateDebugReportUrlsForComponentAuctionWinner();

  // These objects are expected to be set before the function call:
  // - post_auction_signals_
  // - component_level_reporting_data_ (For top level auctions)
  void DispatchReportingRequestForPA(
      absl::string_view dispatch_id,
      const ScoreAdsResponse::AdScore& winning_ad_score,
      const std::shared_ptr<std::string>& auction_config,
      const BuyerReportingMetadata& buyer_reporting_metadata);

  // These objects are expected to be set before the function call:
  // - post_auction_signals_
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
      "PerformReportingWithSellerAndBuyerCodeIsolation instead.")]]
  void PerformReporting(const ScoreAdsResponse::AdScore& winning_ad_score,
                        absl::string_view id);

  // Initializes buyer_reporting_dispatch_request_data_ with the
  // fields included in winning_ad_score.
  void InitializeBuyerReportingDispatchRequestData(
      const ScoreAdsResponse::AdScore& winning_ad_score);

  // Initializes buyer_reporting_dispatch_request_data_ and
  // raw_response_ with buyer reporting IDs included in
  // AdWithBidMetadata.
  void SetReportingIdsInRawResponseAndDispatchRequestData(
      const std::unique_ptr<AdWithBidMetadata>& ad);

  // Performs reportResult and reportWin udf execution with seller and buyer
  // code isolation.
  // These objects are expected to be set before the function call:
  // - buyer_reporting_dispatch_request_data_
  // - ad_data_ / protected_app_signals_ad_data_
  void PerformReportingWithSellerAndBuyerCodeIsolation(
      const ScoreAdsResponse::AdScore& winning_ad_score, absl::string_view id);

  // Performs Private Aggregation Reporting for contribution objects.
  // These objects are expected to be set before the function call:
  // - ad_data_
  // - post_auction_signals_
  // - paapi_contributions_docs_
  void PerformPrivateAggregationReporting(
      absl::string_view most_desirable_ad_score_id);

  // Dispatches request to reportResult() udf with seller and buyer udf
  // isolation.
  // These objects are expected to be set before the function call:
  // - buyer_reporting_dispatch_request_data_
  // - post_auction_signals_
  // - reporting_dispatch_request_config_
  void DispatchReportResultRequest();

  // Dispatches request to reportResult() udf with seller and buyer udf
  // isolation for component auctions.
  // These objects are expected to be set before the function call:
  // - buyer_reporting_dispatch_request_data_
  // - post_auction_signals_
  // - reporting_dispatch_request_config_
  void DispatchReportResultRequestForComponentAuction(
      const ScoreAdsResponse::AdScore& winning_ad_score);

  // Dispatches request to reportResult() udf with seller and buyer udf
  // isolation for top level auctions.
  // These objects are expected to be set before the function call:
  // - raw_response_
  // - post_auction_signals_
  // - component_level_reporting_data_
  // - reporting_dispatch_request_config_
  void DispatchReportResultRequestForTopLevelAuction(
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

  // Function called after reportWin udf execution.
  void ReportWinCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& responses);

  // Adds private aggregation contributions along with adtech_origin from
  // scoreAd and reportResult to the response.
  void SetPrivateAggregationContributionsInResponseForSeller(
      PrivateAggregateReportingResponse pagg_response);

  // Adds private aggregation contributions along with ig index and
  // adtech_origin from reportWin to the response.
  void SetPrivateAggregationContributionsInResponseForBuyer(
      PrivateAggregateReportingResponse pagg_response);

  // Creates and populates dispatch requests using AdWithBidMetadata objects
  // in the input proto for single seller and component auctions.
  void PopulateProtectedAudienceDispatchRequests(
      std::vector<DispatchRequest>& dispatch_requests,
      bool enable_debug_reporting,
      absl::Nullable<
          const absl::flat_hash_map<std::string, rapidjson::StringBuffer>*>
          scoring_signals,
      const std::shared_ptr<std::string>& auction_config,
      google::protobuf::RepeatedPtrField<AdWithBidMetadata>& ads,
      RomaRequestSharedContext& shared_context);

  // Builds score Ad dispatch requests for the component ghost winner ads and
  // adds the requests to `dispatch_requests`.
  //
  // Also stores the relative AdWithBidMetadata in `ad_data_`.
  void BuildScoreAdRequestForComponentGhostWinners(
      bool enable_debug_reporting,
      const std::shared_ptr<std::string>& auction_config,
      AuctionResult& auction_result,
      std::vector<DispatchRequest>& dispatch_requests,
      RomaRequestSharedContext& shared_context);

  // Builds a score Ad dispatch request for the component winner ad and adds the
  // request to `dispatch_requests`.
  //
  // Also stores the relative AdWithBidMetadata in `ad_data_` as well as the
  // component level reporting data in `component_level_reporting_data_`.
  void BuildScoreAdRequestForComponentWinner(
      bool enable_debug_reporting,
      const std::shared_ptr<std::string>& auction_config,
      AuctionResult& auction_result,
      std::vector<DispatchRequest>& dispatch_requests,
      RomaRequestSharedContext& shared_context);

  // Creates and populates dispatch requests using ComponentAuctionResult
  // objects in the input proto for top-level auctions.
  absl::StatusOr<std::vector<DispatchRequest>>
  PopulateTopLevelAuctionDispatchRequests(
      bool enable_debug_reporting,
      const std::shared_ptr<std::string>& auction_config,
      google::protobuf::RepeatedPtrField<AuctionResult>&
          component_auction_results,
      RomaRequestSharedContext& shared_context);

  // Creates and populates dispatch requests using
  // ProtectedAppSignalsAdWithBidMetadata objects
  // in the input proto for single seller auctions auctions
  // if the feature flag is enabled.
  void MayPopulateProtectedAppSignalsDispatchRequests(
      std::vector<DispatchRequest>& dispatch_requests,
      bool enable_debug_reporting,
      absl::Nullable<
          const absl::flat_hash_map<std::string, rapidjson::StringBuffer>*>
          scoring_signals,
      const std::shared_ptr<std::string>& auction_config,
      google::protobuf::RepeatedPtrField<ProtectedAppSignalsAdWithBidMetadata>&
          protected_app_signals_ad_bids,
      RomaRequestSharedContext& shared_context);

  // Validates the ad returned by Roma ScoreAd Response (e.g. validates
  // currency) and sets a rejection reason if the ad is not valid.
  OptionalAdRejectionReason GetAdRejectionReason(
      const rapidjson::Document& response_json,
      const ScoreAdsResponse::AdScore& ad_score);

  // Sets post_auction_signals_ based on the winning ad.
  // These objects are expected to be set before the function call:
  // - raw_response_
  void SetPostAuctionSignals();

  // Adds winner ad to the GRPC response.
  void AddWinnerToResponse(int winner_index, ScoringData& scoring_data,
                           std::vector<ScoredAdData>& parsed_responses);

  // Adds ghost winner ads to the GRPC response.
  void AddGhostWinnersToResponse(const std::vector<int>& ghost_winner_indices,
                                 std::vector<ScoredAdData>& parsed_responses);

  // Performs win and debug reporting (forDebuggingOnly).
  void DoWinAndDebugReporting(bool enable_debug_reporting, int winner_index,
                              const std::vector<ScoredAdData>& parsed_responses,
                              absl::string_view id);

  CLASS_CANCELLATION_WRAPPER(ReportResultCallback, enable_cancellation_,
                             context_, FinishWithStatus)

  grpc::CallbackServerContext* context_;

  // Dispatches execution requests to a library that runs V8 workers in
  // separate processes.
  V8DispatchClient& dispatcher_;

  // The key is the id of the DispatchRequest, and the value is the ad
  // used to create the dispatch request. This map is used to amend each ad's
  // DispatchResponse with more data which is then passed into the final
  // ScoreAdsResponse.
  absl::flat_hash_map<
      std::string,
      std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>>
      ad_data_;

  // The key is the id of the DispatchRequest, and the value is the seller
  // for the component ad. This map is used to add the winning ad's seller
  // to the AdScore object in the response.
  absl::flat_hash_map<std::string, std::string> component_ad_seller_;

  // The key is the id of the DispatchRequest and the value is the k-anon join
  // candidate belonging to the scoring dispatch request. This map is used
  // during top-level multi seller auction.
  absl::flat_hash_map<std::string, KAnonJoinCandidate> ad_k_anon_join_cand_;

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
  RomaRequestContextFactory roma_request_context_factory_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::AuctionContext> metric_context_;

  // Used for debug reporting. Keyed on Roma dispatch ID.
  absl::flat_hash_map<std::string, std::unique_ptr<ScoreAdsResponse::AdScore>>
      ad_scores_;

  // Used for private aggregation reporting. Keyed on Roma dispatch ID.
  absl::flat_hash_map<std::string, rapidjson::Document>
      paapi_contributions_docs_;

  // Used for private aggregation reporting. Map of ghost winner's
  // interest_group_owner (key), interest_group_name (value).
  absl::flat_hash_map<std::string, std::string>
      ghost_winner_interest_group_data_;

  // Flags needed to be passed as input to the code which wraps AdTech provided
  // code.
  bool enable_adtech_code_logging_;
  bool enable_report_result_url_generation_;
  const bool enable_protected_app_signals_;
  bool enable_private_aggregate_reporting_;
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
  bool require_scoring_signals_for_scoring_;
  ReportingDispatchRequestConfig reporting_dispatch_request_config_;
  PostAuctionSignals post_auction_signals_;
  google::protobuf::RepeatedPtrField<std::string>
  GetEmptyAdComponentRenderUrls() {
    static google::protobuf::RepeatedPtrField<std::string>
        empty_ad_component_render_urls;
    return empty_ad_component_render_urls;
  }
  const bool enable_enforce_kanon_;
  absl::flat_hash_set<std::string> buyers_with_report_win_enabled_;
  absl::flat_hash_set<std::string>
      protected_app_signals_buyers_with_report_win_enabled_;
  std::string winning_ad_dispatch_id_;
};
}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_H_
