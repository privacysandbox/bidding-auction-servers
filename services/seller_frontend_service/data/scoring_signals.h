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

#ifndef FLEDGE_SERVICES_SELLER_FRONTEND_SERVICE_DATA_SCORING_SIGNALS_H_
#define FLEDGE_SERVICES_SELLER_FRONTEND_SERVICE_DATA_SCORING_SIGNALS_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
using BuyerBidsResponseMap =
    absl::flat_hash_map<const std::string,
                        std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>;

struct ScoringSignals {
  std::unique_ptr<std::string> scoring_signals;
  uint32_t data_version;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_SELLER_FRONTEND_SERVICE_DATA_SCORING_SIGNALS_H_
