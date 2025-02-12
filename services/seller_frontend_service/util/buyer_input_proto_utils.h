//  Copyright 2025 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_BUYER_INPUT_PROTO_UTILS_H
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_BUYER_INPUT_PROTO_UTILS_H

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::string> ToPrevWinsMs(absl::string_view prev_wins);

BrowserSignalsForBidding ToBrowserSignalsForBidding(
    const BrowserSignals& signals);

BuyerInputForBidding::InterestGroupForBidding ToInterestGroupForBidding(
    const BuyerInput::InterestGroup& interest_group);

BuyerInputForBidding ToBuyerInputForBidding(const BuyerInput& buyer_input);

BuyerInput ToBuyerInput(const BuyerInputForBidding& buyer_input_for_bidding);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_BUYER_INPUT_PROTO_UTILS_H
