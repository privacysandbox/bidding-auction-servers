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

#ifndef SERVICES_COMMON_UTIL_DEBUG_REPORTING_UTIL_H
#define SERVICES_COMMON_UTIL_DEBUG_REPORTING_UTIL_H

#include <memory>

#include "absl/strings/string_view.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/util/post_auction_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

// Captures placeholder data for debug reporting.
struct DebugReportingPlaceholder {
 public:
  // The winning bid from the auction.
  float winning_bid;
  // If the interest group made the winning bid.
  bool made_winning_bid;
  // The bid which was scored second highest in the auction.
  float highest_scoring_other_bid;
  // If the interest group made the highest scoring other bid.
  bool made_highest_scoring_other_bid;

  // In the case where we do not have highest other bid information,
  // we use this constructor.
  explicit DebugReportingPlaceholder(float in_winning_bid,
                                     bool in_made_winning_bid)
      : winning_bid(in_winning_bid),
        made_winning_bid(in_made_winning_bid),
        highest_scoring_other_bid(0.0),
        made_highest_scoring_other_bid(false) {}

  // Constructor with all fields.
  DebugReportingPlaceholder(float in_winning_bid, bool in_made_winning_bid,
                            float in_highest_scoring_other_bid,
                            bool in_made_highest_scoring_other_bid)
      : winning_bid(in_winning_bid),
        made_winning_bid(in_made_winning_bid),
        highest_scoring_other_bid(in_highest_scoring_other_bid),
        made_highest_scoring_other_bid(in_made_highest_scoring_other_bid) {}
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

// Returns a unique pointer to debug reporting placeholder based on the interest
// group owner and post auction signals.
std::unique_ptr<DebugReportingPlaceholder>
GetPlaceholderDataForInterestGroupOwner(
    absl::string_view interest_group_owner,
    const PostAuctionSignals& post_auction_signals);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_DEBUG_REPORTING_UTIL_H
