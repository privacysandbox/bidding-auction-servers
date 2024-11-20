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

#ifndef SERVICES_COMMON_UTIL_POST_AUCTION_SIGNALS_H_
#define SERVICES_COMMON_UTIL_POST_AUCTION_SIGNALS_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
// Captures the signals from the auction winner.
struct PostAuctionSignals {
  // Name of the interest group which won the auction.
  std::string winning_ig_name;
  // Owner of the interest group which won the auction.
  std::string winning_ig_owner;
  // The winning bid from the auction.
  float winning_bid;
  // Currency for said winning bid.
  std::string winning_bid_currency;
  // The winning bid from the auction, in the seller's currency.
  float winning_bid_in_seller_currency;
  // The seller's currency.
  std::string seller_currency;
  // The seller's KV server data version.
  uint32_t seller_data_version;

  // The bid which was scored second highest in the auction.
  float highest_scoring_other_bid;
  // Currency for said highest-scoring other bid.
  std::string highest_scoring_other_bid_currency;
  // Owner of the interest group which made the second highest scored bid.
  std::string highest_scoring_other_bid_ig_owner;
  // Set to true if the signals has second highest scoring bid information.
  bool has_highest_scoring_other_bid;
  // The score of the winning ad
  float winning_score;
  // Render url of the winning ad
  std::string winning_ad_render_url;

  // Map of rejection reasons provided by seller for interest group
  // Key for outer map --> Interest group owner.
  // Key for inner map --> Interest group name.
  absl::flat_hash_map<std::string,
                      absl::flat_hash_map<std::string, SellerRejectionReason>>
      rejection_reason_map;
  // Set to true if the interest group owner
  // was the only bidder that made bids with the second highest score.
  bool made_highest_scoring_other_bid;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_POST_AUCTION_SIGNALS_H_
