//  Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_REPORTING_UTIL_H
#define SERVICES_COMMON_UTIL_REPORTING_UTIL_H

#include <memory>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/post_auction_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kWinningBidPlaceholder = "${winningBid}";
constexpr absl::string_view kWinningBidCurrencyPlaceholder =
    "${winningBidCurrency}";
constexpr absl::string_view kMadeWinningBidPlaceholder = "${madeWinningBid}";
constexpr absl::string_view kHighestScoringOtherBidPlaceholder =
    "${highestScoringOtherBid}";
constexpr absl::string_view kHighestScoringOtherBidCurrencyPlaceholder =
    "${highestScoringOtherBidCurrency}";
constexpr absl::string_view kMadeHighestScoringOtherBidPlaceholder =
    "${madeHighestScoringOtherBid}";
constexpr absl::string_view kRejectReasonPlaceholder = "${rejectReason}";

// Update server_definition.h  - kSellerRejectReasons[] if any change is made to
// SellerRejectionReason Enum.
constexpr absl::string_view kRejectionReasonNotAvailable = "not-available";
constexpr absl::string_view kRejectionReasonInvalidBid = "invalid-bid";
constexpr absl::string_view kRejectionReasonBidBelowAuctionFloor =
    "bid-below-auction-floor";
constexpr absl::string_view kRejectionReasonPendingApprovalByExchange =
    "pending-approval-by-exchange";
constexpr absl::string_view kRejectionReasonDisapprovedByExchange =
    "disapproved-by-exchange";
constexpr absl::string_view kRejectionReasonBlockedByPublisher =
    "blocked-by-publisher";
constexpr absl::string_view kRejectionReasonLanguageExclusions =
    "language-exclusions";
constexpr absl::string_view kRejectionReasonCategoryExclusions =
    "category-exclusions";
constexpr absl::string_view kRejectionReasonBidFromGenBidFailedCurrencyCheck =
    "bid-from-gen-bid-failed-currency-check";
constexpr absl::string_view kRejectionReasonBidFromScoreAdFailedCurrencyCheck =
    "bid-from-score-ad-failed-currency-check";
constexpr absl::string_view kRejectionReasonDidNotMeetTheKAnonymityThreshold =
    "did-not-meet-the-kanonymity-threshold";

constexpr float kDefaultWinningBid = 0.0;
constexpr char kUnknownBidCurrencyCode[] = "???";
constexpr float kDefaultWinningScore = 0.0;
inline constexpr char kDefaultWinningAdRenderUrl[] = "";
inline constexpr char kDefaultWinningInterestGroupName[] = "";
inline constexpr char kDefaultWinningInterestGroupOwner[] = "";
inline constexpr char kDefaultHighestScoringOtherBidInterestGroupOwner[] = "";
inline constexpr char kDefaultHighestScoringOtherBid[] = "0.00";
inline constexpr char kDefaultHasHighestScoringOtherBid[] = "false";
inline constexpr char kFeatureLogging[] = "enable_logging";
inline constexpr char kFeatureDebugUrlGeneration[] =
    "enable_debug_url_generation";

// Captures placeholder data for debug reporting.
struct DebugReportingPlaceholder {
  // The winning bid from the auction.
  float winning_bid;
  // Currency for said bid.
  std::string winning_bid_currency;
  // If the interest group made the winning bid.
  bool made_winning_bid;
  // The bid which was scored second highest in the auction.
  float highest_scoring_other_bid;
  // Currency for said second-highest-scoring bid in the auction.
  std::string highest_scoring_other_bid_currency;
  // If the interest group made the highest scoring other bid.
  bool made_highest_scoring_other_bid;
  // Reason provided by the seller if rejected.
  // Defaults to SELLER_REJECTION_REASON_NOT_AVAILABLE.
  SellerRejectionReason rejection_reason;
};

// Returns post auction signals from winning ad score.
// If there is no winning ad, default values are returned.
PostAuctionSignals GeneratePostAuctionSignals(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score,
    std::string seller_currency, const uint32_t seller_data_version);

// Returns post auction signals from winning ad score for the
// top level seller.
PostAuctionSignals GeneratePostAuctionSignalsForTopLevelSeller(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score);

// Returns a http request object for debug reporting after replacing placeholder
// data in the url.
HTTPRequest CreateDebugReportingHttpRequest(
    absl::string_view url, const DebugReportingPlaceholder& placeholder_data,
    bool is_winning_interest_group);

// Returns debug reporting url after replacing the placeholder data in the URL.
std::string CreateDebugReportingUrlForInterestGroup(
    absl::string_view debug_url,
    const DebugReportingPlaceholder& placeholder_data,
    bool is_winning_interest_group);

// Returns a debug reporting placeholder for the interest group.
DebugReportingPlaceholder GetPlaceholderDataForInterestGroup(
    absl::string_view interest_group_owner,
    absl::string_view interest_group_name,
    const PostAuctionSignals& post_auction_signals);

// Converts a given string to Rejection Reason based on allowed values.
// Default is always
// SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE.
SellerRejectionReason ToSellerRejectionReason(
    absl::string_view rejection_reason_str);

// Converts a given SellerRejectionReason to string.
// Default is always "not-available".
absl::string_view ToSellerRejectionReasonString(
    SellerRejectionReason rejection_reason);

// Captures the logs that are generated by the ad techs into our log streams if
// `enable_ad_tech_code_logging` is enabled.
void MayVlogAdTechCodeLogs(bool enable_ad_tech_code_logging,
                           const rapidjson::Document& document,
                           RequestLogContext& log_context);

// Parses the JSON string, conditionally prints the logs from the response and
// returns a serialized response string retrieved from the underlying UDF.
absl::StatusOr<std::string> ParseAndGetResponseJson(
    bool enable_ad_tech_code_logging, const std::string& response,
    RequestLogContext& log_context);

// Parses the JSON array of string, conditionally prints the logs from the
// response and returns a serialized response vector retrieved from the
// underlying UDF.
absl::StatusOr<std::vector<std::string>> ParseAndGetResponseJsonArray(
    bool enable_ad_tech_code_logging, const std::string& response,
    RequestLogContext& log_context);

// Returns a JSON string for feature flags to be used by the wrapper script.
std::string GetFeatureFlagJson(bool enable_logging,
                               bool enable_debug_url_generation);

}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_COMMON_UTIL_REPORTING_UTIL_H
