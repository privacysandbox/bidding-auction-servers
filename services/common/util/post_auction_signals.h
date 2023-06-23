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

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
// Captures the signals from the auction winner.
struct PostAuctionSignals {
 public:
  // Name of the interest group which won the auction.
  absl::string_view winning_ig_name;
  // Owner of the interest group which won the auction.
  absl::string_view winning_ig_owner;
  // The winning bid from the auction.
  float winning_bid;

  // The bid which was scored second highest in the auction.
  float highest_scoring_other_bid;
  // Owner of the interest group which made the second highest scored bid.
  absl::string_view highest_scoring_other_bid_ig_owner;
  // Set to true if the signals has second highest scoring bid information.
  bool has_highest_scoring_other_bid;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_POST_AUCTION_SIGNALS_H_
