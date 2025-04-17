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
#include "services/common/reporters/async_reporter.h"
#include "services/common/util/post_auction_signals.h"
#include "services/common/util/random_number_generator.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kWinningBidPlaceholder = "${winningBid}";
inline constexpr absl::string_view kWinningBidCurrencyPlaceholder =
    "${winningBidCurrency}";
inline constexpr absl::string_view kMadeWinningBidPlaceholder =
    "${madeWinningBid}";
inline constexpr absl::string_view kHighestScoringOtherBidPlaceholder =
    "${highestScoringOtherBid}";
inline constexpr absl::string_view kHighestScoringOtherBidCurrencyPlaceholder =
    "${highestScoringOtherBidCurrency}";
inline constexpr absl::string_view kMadeHighestScoringOtherBidPlaceholder =
    "${madeHighestScoringOtherBid}";
inline constexpr absl::string_view kRejectReasonPlaceholder = "${rejectReason}";

// Update server_definition.h  - kBuyerDebugUrlStatus[] and/or
// kSellerDebugUrlStatus[] if any other reason to drop a debug URL is added.
inline constexpr absl::string_view kDebugUrlRejectedDuringSampling =
    "[Rejected] Not selected during 1/1000 sampling";
inline constexpr absl::string_view kDebugUrlRejectedForExceedingSize =
    "[Rejected] Size exceeded max allowed size per URL";
inline constexpr absl::string_view kDebugUrlRejectedForExceedingTotalSize =
    "[Rejected] Total size of received URLs exceeded max allowed total size "
    "across all URLs";
inline constexpr absl::string_view kBuyerDebugUrlSentToSeller =
    "[Validated] Sent to seller";

// Update server_definition.h  - kSellerRejectReasons[] if any change is made to
// SellerRejectionReason Enum.
inline constexpr absl::string_view kRejectionReasonNotAvailable =
    "not-available";
inline constexpr absl::string_view kRejectionReasonInvalidBid = "invalid-bid";
inline constexpr absl::string_view kRejectionReasonBidBelowAuctionFloor =
    "bid-below-auction-floor";
inline constexpr absl::string_view kRejectionReasonPendingApprovalByExchange =
    "pending-approval-by-exchange";
inline constexpr absl::string_view kRejectionReasonDisapprovedByExchange =
    "disapproved-by-exchange";
inline constexpr absl::string_view kRejectionReasonBlockedByPublisher =
    "blocked-by-publisher";
inline constexpr absl::string_view kRejectionReasonLanguageExclusions =
    "language-exclusions";
inline constexpr absl::string_view kRejectionReasonCategoryExclusions =
    "category-exclusions";
inline constexpr absl::string_view
    kRejectionReasonBidFromGenBidFailedCurrencyCheck =
        "bid-from-gen-bid-failed-currency-check";
inline constexpr absl::string_view
    kRejectionReasonBidFromScoreAdFailedCurrencyCheck =
        "bid-from-score-ad-failed-currency-check";
inline constexpr absl::string_view
    kRejectionReasonDidNotMeetTheKAnonymityThreshold =
        "did-not-meet-the-kanonymity-threshold";

inline constexpr float kDefaultWinningBid = 0.0;
inline constexpr char kUnknownBidCurrencyCode[] = "???";
inline constexpr float kDefaultWinningScore = 0.0;
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

struct DebugReportsInResponseCounts {
  int allowed_win_report_count = 0;
  int allowed_loss_report_count = 0;
  int allowed_report_count = 0;

  // Factory method to create limits based on debug reporting mode.
  static DebugReportsInResponseCounts Create(bool enable_sampled_reporting,
                                             bool is_component_auction) {
    DebugReportsInResponseCounts limits;
    if (enable_sampled_reporting) {
      // Sampled component auction: 1 win + 1 loss.
      // Sampled single-seller auction: 1 win or 1 loss.
      limits.allowed_win_report_count = 1;
      limits.allowed_loss_report_count = 1;
      limits.allowed_report_count = is_component_auction ? 2 : 1;
    } else {
      // Unsampled component auction: 2 buyer win & loss + 2 seller win & loss.
      limits.allowed_win_report_count = 2;
      limits.allowed_loss_report_count = 2;
      limits.allowed_report_count = 4;
    }
    return limits;
  }

  // Check if a non-empty debug report can be added within limits.
  bool CanAddDebugReport(const DebugReports::DebugReport& debug_report) const {
    if (debug_report.url().empty()) {
      return true;
    }
    if (allowed_report_count <= 0) {
      return false;
    }
    return debug_report.is_win_report() ? (allowed_win_report_count > 0)
                                        : (allowed_loss_report_count > 0);
  }

  // Decrement limits for a new non-empty debug report.
  void DecrementCountsForDebugReport(
      const DebugReports::DebugReport& debug_report) {
    if (debug_report.url().empty()) {
      return;
    }
    allowed_report_count -= 1;
    if (debug_report.is_win_report()) {
      allowed_win_report_count -= 1;
    } else {
      allowed_loss_report_count -= 1;
    }
  }
};

// Returns true with 1/sampling_upper_bound probability and false otherwise.
// This determines if a debug report url is selected during random sampling.
inline bool DebugUrlPassesSampling(int sampling_upper_bound) {
  if (absl::StatusOr<int> random_number = RandInt(1, sampling_upper_bound);
      random_number.ok()) {
    return *random_number == 1;
  }
  return false;
}

// Returns post auction signals from winning ad score.
// If there is no winning ad, default values are returned.
PostAuctionSignals GeneratePostAuctionSignals(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score,
    std::string seller_currency, const uint32_t seller_data_version);

// Returns post auction signals from winning ad score for the
// top level seller.
PostAuctionSignals GeneratePostAuctionSignalsForTopLevelSeller(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score);

// Sends a debug reporting ping to the specified debug url.
void SendDebugReportingPing(const AsyncReporter& async_reporter,
                            absl::string_view debug_url,
                            absl::string_view ig_owner,
                            absl::string_view ig_name, bool is_winning_ig,
                            const PostAuctionSignals& post_auction_signals);

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
