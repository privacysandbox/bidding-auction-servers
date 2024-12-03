// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_

#include <memory>
#include <string>
#include <utility>

#include "api/bidding_auction_servers.pb.h"
#include "services/seller_frontend_service/data/scoring_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

struct InterestGroupIdentity {
  std::string interest_group_owner;
  std::string interest_group_name;
  bool operator==(const InterestGroupIdentity& other) const {
    return interest_group_owner == other.interest_group_owner &&
           interest_group_name == other.interest_group_name;
  }

  // Enable Abseil hashing
  template <typename H>
  friend H AbslHashValue(H h, const InterestGroupIdentity& ig) {
    return H::combine(std::move(h), ig.interest_group_owner,
                      ig.interest_group_name);
  }
};

// Handles private aggregation contributions filtering and post processing.
void HandlePrivateAggregationContributions(
    const absl::flat_hash_map<InterestGroupIdentity, int>&
        interest_group_index_map,
    ScoreAdsResponse::AdScore& high_score,
    BuyerBidsResponseMap& shared_buyer_bids_map);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_
