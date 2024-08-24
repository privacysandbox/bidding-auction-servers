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

#ifndef SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_POST_AUCTION_UTIL_H_
#define SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_POST_AUCTION_UTIL_H_

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr std::string_view kInvalidPrivateAggregationValueType =
    "Invalid Private Aggregation Value with neither Int or Extended Value.";
inline constexpr std::string_view kBaseValueNotSupported =
    "Input base value is not supported.";
struct BaseValues {
  float winning_bid;
  float highest_scoring_other_bid;
  SellerRejectionReason reject_reason;
};

// Handle Private Aggregation Value post auction to convert all signalValue into
// numerical values.
// - If Private Aggregation Value is already numerical, no operation is done.
// - If Private Aggregation Value is signalValue dependent on post auction
// signals, final numerical value is calculated with (baseValue * scale) +
// offset
absl::Status HandlePrivateAggregationValuePostAuction(
    const BaseValues& base_values,
    PrivateAggregationValue& private_aggregation_value);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_POST_AUCTION_UTIL_H_
