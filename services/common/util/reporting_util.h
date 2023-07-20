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

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/util/post_auction_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kWinningBidPlaceholder = "${winningBid}";
constexpr absl::string_view kMadeWinningBidPlaceholder = "${madeWinningBid}";
constexpr absl::string_view kHighestScoringOtherBidPlaceholder =
    "${highestScoringOtherBid}";
constexpr absl::string_view kMadeHighestScoringOtherBidPlaceholder =
    "${madeHighestScoringOtherBid}";
constexpr absl::string_view kRejectReasonPlaceholder = "${rejectReason}";

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

constexpr float kDefaultWinningBid = 0.0;
constexpr float kDefaultWinningScore = 0.0;
constexpr absl::string_view kDefaultWinningAdRenderUrl = "";
constexpr absl::string_view kDefaultWinningIntereseGroupName = "";
constexpr absl::string_view kDefaultWinningInterestGroupOwner = "";
constexpr absl::string_view kDefaultHighestScoringOtherBidInterestGroupOwner =
    "";
constexpr float kDefaultHighestScoringOtherBid = 0.0;
constexpr float kDefaultHasHighestScoringOtherBid = false;

// Captures placeholder data for debug reporting.
struct DebugReportingPlaceholder {
  // The winning bid from the auction.
  float winning_bid;
  // If the interest group made the winning bid.
  bool made_winning_bid;
  // The bid which was scored second highest in the auction.
  float highest_scoring_other_bid;
  // If the interest group made the highest scoring other bid.
  bool made_highest_scoring_other_bid;
  // Reason provided by the seller if rejected.
  // Defaults to SELLER_REJECTION_REASON_NOT_AVAILABLE.
  SellerRejectionReason rejection_reason;

  // Constructor with all fields.
  explicit DebugReportingPlaceholder(float winning_bid, bool made_winning_bid,
                                     float highest_scoring_other_bid,
                                     bool made_highest_scoring_other_bid,
                                     SellerRejectionReason rejection_reason) {
    this->winning_bid = winning_bid;
    this->made_winning_bid = made_winning_bid;
    this->highest_scoring_other_bid = highest_scoring_other_bid;
    this->made_highest_scoring_other_bid = made_highest_scoring_other_bid;
    this->rejection_reason = rejection_reason;
  }
};

// Returns a unique pointer to post auction signals from winning ad score.
// If there is no winning ad, default values are set.
std::unique_ptr<PostAuctionSignals> GeneratePostAuctionSignals(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score);

// Returns a http request object for debug reporting after replacing placeholder
// data in the url.
HTTPRequest CreateDebugReportingHttpRequest(
    absl::string_view url,
    std::unique_ptr<DebugReportingPlaceholder> placeholder_data);

// Returns a unique pointer to debug reporting placeholder for the interest
// group.
std::unique_ptr<DebugReportingPlaceholder> GetPlaceholderDataForInterestGroup(
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

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_REPORTING_UTIL_H
