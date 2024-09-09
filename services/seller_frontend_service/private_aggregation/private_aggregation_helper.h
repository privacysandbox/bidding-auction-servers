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

#include "api/bidding_auction_servers.pb.h"
#include "services/seller_frontend_service/data/scoring_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

// Handles private aggregation contributions filtering and post processing.
void HandlePrivateAggregationContributions(
    ScoreAdsResponse::AdScore& high_score,
    BuyerBidsResponseMap& shared_buyer_bids_map);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_
