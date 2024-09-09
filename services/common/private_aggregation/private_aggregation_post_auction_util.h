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

#include <optional>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kInvalidPrivateAggregationBucketType =
    "Invalid Private Aggregation Bucket with neither bucket_128_bit or "
    "signal_bucket.";
inline constexpr absl::string_view kPrivateAggregationBucketNegativeFinalValue =
    "Invalid Private Aggregation Signal Bucket BaseValue * scale - offset < 0.";
inline constexpr absl::string_view kInvalidPrivateAggregationValueType =
    "Invalid Private Aggregation Value with neither Int or Extended Value.";
inline constexpr absl::string_view kBaseValueNotSupported =
    "Input base value is not supported.";

inline constexpr absl::string_view kBaseValuesRejectReasonNotAvailable =
    "Reject reason is not available.";
struct BaseValues {
  float winning_bid;
  float highest_scoring_other_bid;
  std::optional<SellerRejectionReason> reject_reason;
};

// Calculates and returns the final 128-bit bucket value for private aggregation
// based on the provided BaseValues and PrivateAggregationBucket.
//
// If the bucket 128 bit field is already set, the function simply returns its
// value.
//
// If the bucket contains signal bucket, the function calculates the final
// bucket value using the following equation:
//   final_value = base_value_numerical_value * scale + offset
//
// where:
//   * base_value_numerical_value is determined by the base_value field of the
//     signal_bucket and the corresponding value in the BaseValues object.
//   * scale is the scale field of the signal_bucket.
//   * offset is the offset field of the signal_bucket.
//
// The final_value is then converted to a Bucket128Bit object and returned.
//
// Args:
//   base_values: The BaseValues object containing the winning bid, highest
//     scoring other bid, and other relevant values.
//   bucket: The PrivateAggregationBucket object containing the bucket
//     configuration.
//
// Returns:
//   An absl::StatusOr<Bucket128Bit> object containing the final 128-bit
//   bucket value, or an error status if the calculation fails.
absl::StatusOr<Bucket128Bit> GetPrivateAggregationBucketPostAuction(
    const BaseValues& base_values,
    const PrivateAggregationBucket& private_aggregation_bucket);

// Convert Private Aggregation Value to integer value after post auction signals
// are available.
// - If Private Aggregation Value is already an integer value, no operation is
// done.
// - If Private Aggregation Value is signalValue dependent on post auction
// signals, final integer value is calculated with (baseValue * scale) +
// offset
// Returns:
//   - the integer value if the conversion is successful.
//   - absl::InvalidArgumentError(kInvalidPrivateAggregationValueType) if the
//     Private Aggregation Value is invalid.
//   - absl::InvalidArgumentError(kBaseValueNotSupported) if the base value is
//     not supported.
//   - absl::InvalidArgumentError(kBaseValuesRejectReasonNotAvailable) if the
//     base value is BaseValue::BASE_VALUE_BID_REJECTION_REASON and the
//     reject_reason is not available in base_values. This happens when the ad
//     is not rejected but reject reason is specified as placeholder base value.
absl::StatusOr<int> GetPrivateAggregationValuePostAuction(
    const BaseValues& base_values,
    const PrivateAggregationValue& private_aggregation_value);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_POST_AUCTION_UTIL_H_
