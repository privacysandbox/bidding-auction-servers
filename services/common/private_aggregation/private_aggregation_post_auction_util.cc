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

#include "private_aggregation_post_auction_util.h"

#include "rapidjson/document.h"
#include "services/common/util/reporting_util.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::Status HandlePrivateAggregationValuePostAuction(
    const BaseValues& base_values,
    PrivateAggregationValue& private_aggregation_value) {
  if (private_aggregation_value.has_int_value()) {
    return absl::OkStatus();
  }

  if (!private_aggregation_value.has_extended_value()) {
    return absl::InvalidArgumentError(kInvalidPrivateAggregationValueType);
  }

  BaseValue base_value =
      private_aggregation_value.extended_value().base_value();
  int final_value = 0;

  switch (base_value) {
    case 1:
      final_value = base_values.winning_bid;
      break;
    case 2:
      final_value = base_values.highest_scoring_other_bid;
      break;
    case 5:
      final_value = base_values.reject_reason;
      break;
    default:
      return absl::InvalidArgumentError(kBaseValueNotSupported);
  }

  final_value =
      static_cast<int>(final_value *
                       private_aggregation_value.extended_value().scale()) +
      private_aggregation_value.extended_value().offset();
  private_aggregation_value.set_int_value(final_value);
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
