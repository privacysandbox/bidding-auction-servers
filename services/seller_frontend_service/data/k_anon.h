//  Copyright 2024 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_DATA_K_ANON_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_DATA_K_ANON_H_

#include <memory>
#include <string>
#include <vector>

#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

using KAnonGhostWinners = std::vector<AuctionResult::KAnonGhostWinner>;

// Data related to k-anon winner/ghost winner that is to be populated in the
// AuctionResult returned to the client.
struct KAnonAuctionResultData {
  std::unique_ptr<KAnonGhostWinners> kanon_ghost_winners = nullptr;
  std::unique_ptr<KAnonJoinCandidate> kanon_winner_join_candidates = nullptr;
  int kanon_winner_positional_index = -1;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_DATA_K_ANON_H_
