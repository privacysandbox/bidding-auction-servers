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

#include "absl/numeric/int128.h"
#include "rapidjson/document.h"
#include "services/common/util/reporting_util.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<Bucket128Bit> GetPrivateAggregationBucketPostAuction(
    const BaseValues& base_values, const PrivateAggregationBucket& bucket) {
  Bucket128Bit result;
  if (bucket.has_bucket_128_bit()) {
    result.CopyFrom(bucket.bucket_128_bit());
    return result;
  }

  if (!bucket.has_signal_bucket()) {
    return absl::InvalidArgumentError(kInvalidPrivateAggregationBucketType);
  }

  BaseValue base_value = bucket.signal_bucket().base_value();

  // base_value indicates which value the browser
  // should use to calculate the resulting bucket or value.
  // For example, if base_value is BASE_VALUE_WINNING_BID, the extended value is
  // the winning bid * scale + offset.
  // For more information, see
  // https://github.com/WICG/turtledove/blob/44a00e4e8f02ec03c05075fd994a8abad7b2a6cd/FLEDGE_extended_PA_reporting.md?plain=1#L192.
  float base_value_numerical_value;

  switch (base_value) {
    case BaseValue::BASE_VALUE_WINNING_BID:
      base_value_numerical_value = base_values.winning_bid;
      break;
    case BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID:
      base_value_numerical_value = base_values.highest_scoring_other_bid;
      break;
    case BaseValue::BASE_VALUE_BID_REJECTION_REASON:
      if (!base_values.reject_reason) {
        return absl::InvalidArgumentError(kBaseValuesRejectReasonNotAvailable);
      }
      base_value_numerical_value = base_values.reject_reason.value();
      break;
    default:
      return absl::InvalidArgumentError(kBaseValueNotSupported);
  }

  // Calculate the offset value as a 128-bit integer.
  absl::uint128 offset_value =
      absl::MakeUint128(bucket.signal_bucket().offset().value(1),
                        bucket.signal_bucket().offset().value(0));

  absl::uint128 final_value = absl::MakeUint128(
      0, static_cast<uint64_t>(base_value_numerical_value *
                               bucket.signal_bucket().scale()));
  if (bucket.signal_bucket().offset().is_negative()) {
    if (final_value < offset_value) {
      return absl::InvalidArgumentError(
          kPrivateAggregationBucketNegativeFinalValue);
    }
    final_value -= offset_value;
  } else {
    final_value += offset_value;
  }

  // Convert the final value to an array of 2 uint64 integers.
  result.add_bucket_128_bits(absl::Uint128Low64(final_value));
  result.add_bucket_128_bits(absl::Uint128High64(final_value));

  return result;
}

absl::StatusOr<int> GetPrivateAggregationValuePostAuction(
    const BaseValues& base_values,
    const PrivateAggregationValue& private_aggregation_value) {
  if (private_aggregation_value.has_int_value()) {
    return private_aggregation_value.int_value();
  }

  if (!private_aggregation_value.has_extended_value()) {
    return absl::InvalidArgumentError(kInvalidPrivateAggregationValueType);
  }

  BaseValue base_value =
      private_aggregation_value.extended_value().base_value();
  int final_value = 0;

  // base_value indicates which value the browser
  // should use to calculate the resulting bucket or value.
  // For example, if base_value is BASE_VALUE_WINNING_BID, the extended value is
  // the winning bid
  // * scale + offset.
  // For more information, see
  // https://github.com/WICG/turtledove/blob/44a00e4e8f02ec03c05075fd994a8abad7b2a6cd/FLEDGE_extended_PA_reporting.md?plain=1#L192.
  switch (base_value) {
    case BaseValue::BASE_VALUE_WINNING_BID:
      final_value = base_values.winning_bid;
      break;
    case BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID:
      final_value = base_values.highest_scoring_other_bid;
      break;
    case BaseValue::BASE_VALUE_BID_REJECTION_REASON:
      if (!base_values.reject_reason) {
        return absl::InvalidArgumentError(kBaseValuesRejectReasonNotAvailable);
      }
      final_value = base_values.reject_reason.value();
      break;
    default:
      return absl::InvalidArgumentError(kBaseValueNotSupported);
  }

  final_value =
      static_cast<int>(final_value *
                       private_aggregation_value.extended_value().scale()) +
      private_aggregation_value.extended_value().offset();
  return final_value;
}

}  // namespace privacy_sandbox::bidding_auction_servers
