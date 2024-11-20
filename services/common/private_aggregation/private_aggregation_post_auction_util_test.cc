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

#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"

#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api/bidding_auction_servers.pb.h"
#include "include/gtest/gtest.h"
#include "services/common/util/post_auction_signals.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

template <typename T>
inline constexpr int EnumValue(T arg) {
  return static_cast<std::underlying_type_t<T>>(arg);
}

void TestGetPrivateAggregationBucketPostAuction(
    const BaseValues& base_values, BaseValue base_value, float scale,
    const std::vector<uint64_t>& offset, bool is_negative_offset,
    const std::vector<uint64_t>& expected_bucket) {
  PrivateAggregationBucket bucket;
  bucket.mutable_signal_bucket()->set_base_value(base_value);
  bucket.mutable_signal_bucket()->set_scale(scale);
  for (uint64_t value : offset) {
    bucket.mutable_signal_bucket()->mutable_offset()->add_value(value);
  }
  bucket.mutable_signal_bucket()->mutable_offset()->set_is_negative(
      is_negative_offset);

  absl::StatusOr<Bucket128Bit> result =
      GetPrivateAggregationBucketPostAuction(base_values, bucket);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->bucket_128_bits_size(), expected_bucket.size());
  for (size_t i = 0; i < expected_bucket.size(); ++i) {
    EXPECT_EQ(result->bucket_128_bits(i), expected_bucket[i]);
  }
}

void TestGetPrivateAggregationValuePostAuction(BaseValues base_values,
                                               BaseValue base_value,
                                               double scale, int offset,
                                               int expected_value) {
  PrivateAggregationValue private_aggregation_value;

  private_aggregation_value.mutable_extended_value()->set_base_value(
      base_value);
  private_aggregation_value.mutable_extended_value()->set_scale(scale);
  private_aggregation_value.mutable_extended_value()->set_offset(offset);

  EXPECT_EQ(GetPrivateAggregationValuePostAuction(base_values,
                                                  private_aggregation_value)
                .value(),
            expected_value);
}

TEST(GetPrivateAggregationBucketPostAuctionTest, SupportsPositiveOffset) {
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  TestGetPrivateAggregationBucketPostAuction(base_values,
                                             BaseValue::BASE_VALUE_WINNING_BID,
                                             2.0f, {1, 0}, false, {21, 0});
}

TEST(GetPrivateAggregationBucketPostAuctionTest, SupportsNegativeOffset) {
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  TestGetPrivateAggregationBucketPostAuction(base_values,
                                             BaseValue::BASE_VALUE_WINNING_BID,
                                             2.0f, {1, 0}, true, {19, 0});
}

TEST(GetPrivateAggregationBucketPostAuctionTest, Supports128BitAlreadySet) {
  Bucket128Bit bucket_128_bit;
  bucket_128_bit.add_bucket_128_bits(1);
  bucket_128_bit.add_bucket_128_bits(2);
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  PrivateAggregationBucket bucket;
  *bucket.mutable_bucket_128_bit() = bucket_128_bit;

  absl::StatusOr<Bucket128Bit> result =
      GetPrivateAggregationBucketPostAuction(base_values, bucket);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->bucket_128_bits(0), 1);
  EXPECT_EQ(result->bucket_128_bits(1), 2);
}

TEST(GetPrivateAggregationBucketPostAuctionTest,
     UnsupportedBaseValueReturnFails) {
  PrivateAggregationBucket bucket;
  bucket.mutable_signal_bucket()->set_base_value(
      BaseValue::BASE_VALUE_SCRIPT_RUN_TIME);
  bucket.mutable_signal_bucket()->set_scale(2.0f);
  bucket.mutable_signal_bucket()->mutable_offset()->add_value(1);
  bucket.mutable_signal_bucket()->mutable_offset()->add_value(0);
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;
  EXPECT_EQ(
      GetPrivateAggregationBucketPostAuction(base_values, bucket).status(),
      absl::Status(absl::StatusCode::kInvalidArgument, kBaseValueNotSupported));
}

TEST(GetPrivateAggregationBucketPostAuctionTest,
     InvalidPrivateAggregationBucketFails) {
  PrivateAggregationBucket bucket;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;
  EXPECT_EQ(
      GetPrivateAggregationBucketPostAuction(base_values, bucket).status(),
      absl::Status(absl::StatusCode::kInvalidArgument,
                   kInvalidPrivateAggregationBucketType));
}

TEST(GetPrivateAggregationBucketPostAuctionTest,
     NeedRejectionReasonForSignalBucketTypeRejectionReason) {
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;

  PrivateAggregationBucket bucket;
  bucket.mutable_signal_bucket()->set_base_value(
      BaseValue::BASE_VALUE_BID_REJECTION_REASON);
  bucket.mutable_signal_bucket()->set_scale(2.0f);
  bucket.mutable_signal_bucket()->mutable_offset()->add_value(1);
  bucket.mutable_signal_bucket()->mutable_offset()->add_value(0);

  EXPECT_EQ(
      GetPrivateAggregationBucketPostAuction(base_values, bucket).status(),
      absl::Status(absl::StatusCode::kInvalidArgument,
                   kBaseValuesRejectReasonNotAvailable));
}

TEST(GetPrivateAggregationValuePostAuctionTest, IntValueAlreadySet) {
  PrivateAggregationValue private_aggregation_value;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  // int_value is already set
  private_aggregation_value.set_int_value(10);
  ASSERT_THAT(GetPrivateAggregationValuePostAuction(base_values,
                                                    private_aggregation_value),
              private_aggregation_value.int_value());
}

TEST(GetPrivateAggregationValuePostAuctionTest,
     GetFinalValueBaseValueWinningBid) {
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  double scale = 2.0;
  int offset = 1;

  TestGetPrivateAggregationValuePostAuction(
      base_values, BaseValue::BASE_VALUE_WINNING_BID, scale, offset,
      static_cast<int>(base_values.winning_bid * scale + offset));
}

TEST(GetPrivateAggregationValuePostAuctionTest,
     GetFinalValueBaseValueHighestScoringOtherBid) {
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  double scale = 2.0;
  int offset = 1;

  TestGetPrivateAggregationValuePostAuction(
      base_values, BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID, scale,
      offset,
      static_cast<int>(base_values.highest_scoring_other_bid * scale + offset));
  TestGetPrivateAggregationValuePostAuction(
      base_values, BaseValue::BASE_VALUE_BID_REJECTION_REASON, scale, offset,
      static_cast<int>(EnumValue(base_values.reject_reason.value()) * scale +
                       offset));
}

TEST(GetPrivateAggregationValuePostAuctionTest,
     GetFinalValueBaseValueBidRejectionReason) {
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  double scale = 2.0;
  int offset = 1;

  TestGetPrivateAggregationValuePostAuction(
      base_values, BaseValue::BASE_VALUE_BID_REJECTION_REASON, scale, offset,
      static_cast<int>(EnumValue(base_values.reject_reason.value()) * scale +
                       offset));
}

TEST(GetPrivateAggregationValuePostAuctionTest, UnsupportedBaseValue) {
  PrivateAggregationValue private_aggregation_value;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  // extended_value is set with unsupported base_value =
  // BaseValue::BASE_VALUE_SCRIPT_RUN_TIME
  private_aggregation_value.mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_SCRIPT_RUN_TIME);
  private_aggregation_value.mutable_extended_value()->set_scale(2.0f);
  private_aggregation_value.mutable_extended_value()->set_offset(1.0f);
  ASSERT_THAT(
      GetPrivateAggregationValuePostAuction(base_values,
                                            private_aggregation_value),
      absl::Status(absl::StatusCode::kInvalidArgument, kBaseValueNotSupported));
}

TEST(GetPrivateAggregationValuePostAuctionTest,
     GetPrivateAggregationValuePostAuctionInvalidValue) {
  PrivateAggregationValue private_aggregation_value;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;
  ASSERT_THAT(GetPrivateAggregationValuePostAuction(base_values,
                                                    private_aggregation_value),
              absl::Status(absl::StatusCode::kInvalidArgument,
                           kInvalidPrivateAggregationValueType));
}

TEST(GetPrivateAggregationValuePostAuctionTest,
     ReturnsInValidArgumentIfRejectReasonNotAvailable) {
  PrivateAggregationValue private_aggregation_value;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;

  BaseValue base_value = BaseValue::BASE_VALUE_BID_REJECTION_REASON;
  double scale = 2.0;
  int offset = 1;

  private_aggregation_value.mutable_extended_value()->set_base_value(
      base_value);
  private_aggregation_value.mutable_extended_value()->set_scale(scale);
  private_aggregation_value.mutable_extended_value()->set_offset(offset);

  EXPECT_THAT(GetPrivateAggregationValuePostAuction(base_values,
                                                    private_aggregation_value),
              absl::Status(absl::StatusCode::kInvalidArgument,
                           kBaseValuesRejectReasonNotAvailable));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
