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

#include "services/common/util/reporting_util.h"

#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view winning_bid_placeholder = "${winningBid}";
constexpr absl::string_view made_winning_bid_placeholder = "${madeWinningBid}";
constexpr absl::string_view highest_scoring_other_bid_placeholder =
    "${highestScoringOtherBid}";
constexpr absl::string_view made_highest_scoring_other_bid_placeholder =
    "${madeHighestScoringOtherBid}";
constexpr absl::string_view reject_reason_placeholder = "${rejectReason}";

std::unique_ptr<PostAuctionSignals> GeneratePostAuctionSignals(
    const std::optional<ScoreAdsResponse::AdScore>& winning_ad_score) {
  std::unique_ptr<PostAuctionSignals> signals =
      std::make_unique<PostAuctionSignals>();
  if (!winning_ad_score.has_value()) {
    signals->winning_bid = 0.0;
    signals->winning_ig_owner = "";
    signals->winning_ig_name = "";
    signals->has_highest_scoring_other_bid = false;
    signals->highest_scoring_other_bid = 0.0;
    signals->highest_scoring_other_bid_ig_owner = "";
  } else {
    signals->winning_bid = winning_ad_score->buyer_bid();
    signals->winning_ig_owner = winning_ad_score->interest_group_owner();
    signals->winning_ig_name = winning_ad_score->interest_group_name();
    if (winning_ad_score->ig_owner_highest_scoring_other_bids_map().size() >
        0) {
      auto iterator =
          winning_ad_score->ig_owner_highest_scoring_other_bids_map().begin();
      if (iterator->second.values().size() > 0) {
        signals->has_highest_scoring_other_bid = true;
        signals->highest_scoring_other_bid =
            iterator->second.values().Get(0).number_value();
        signals->highest_scoring_other_bid_ig_owner = iterator->first;
      } else {
        signals->has_highest_scoring_other_bid = false;
        signals->highest_scoring_other_bid = 0.0;
        signals->highest_scoring_other_bid_ig_owner = "";
      }
    } else {
      signals->has_highest_scoring_other_bid = false;
    }
  }
  return signals;
}

HTTPRequest CreateDebugReportingHttpRequest(
    absl::string_view url,
    std::unique_ptr<DebugReportingPlaceholder> placeholder_data) {
  std::string formatted_url = absl::StrReplaceAll(
      url,
      {{winning_bid_placeholder, absl::StrCat(placeholder_data->winning_bid)},
       {made_winning_bid_placeholder,
        placeholder_data->made_winning_bid ? "true" : "false"},
       {highest_scoring_other_bid_placeholder,
        absl::StrCat(placeholder_data->highest_scoring_other_bid)},
       {made_highest_scoring_other_bid_placeholder,
        placeholder_data->made_highest_scoring_other_bid ? "true" : "false"}});
  HTTPRequest http_request;
  http_request.url = formatted_url;
  http_request.headers = {};
  return http_request;
}

std::unique_ptr<DebugReportingPlaceholder>
GetPlaceholderDataForInterestGroupOwner(
    absl::string_view interest_group_owner,
    const PostAuctionSignals& post_auction_signals) {
  bool made_winning_bid = false;
  if (interest_group_owner == post_auction_signals.winning_ig_owner) {
    made_winning_bid = true;
  }
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      std::make_unique<DebugReportingPlaceholder>(
          post_auction_signals.winning_bid, made_winning_bid);
  if (post_auction_signals.has_highest_scoring_other_bid) {
    placeholder->made_highest_scoring_other_bid =
        interest_group_owner ==
                post_auction_signals.highest_scoring_other_bid_ig_owner
            ? true
            : false;
    placeholder->highest_scoring_other_bid =
        post_auction_signals.highest_scoring_other_bid;
  }
  return placeholder;
}
}  // namespace privacy_sandbox::bidding_auction_servers
