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

#include "api/bidding_auction_servers.pb.h"
#include "include/gtest/gtest.h"
#include "services/common/util/post_auction_signals.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(PrivateAggregationPostAuctionUtilTest,
     HandlePrivateAggregationValuePostAuctionSuccess) {
  PrivateAggregationValue private_aggregation_value;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;

  // int_value is already set
  private_aggregation_value.set_int_value(10);
  ASSERT_TRUE(private_aggregation_value.has_int_value());
  ASSERT_EQ(private_aggregation_value.int_value(), 10);
  ASSERT_THAT(HandlePrivateAggregationValuePostAuction(
                  base_values, private_aggregation_value),
              absl::OkStatus());
  ASSERT_EQ(private_aggregation_value.int_value(), 10);

  // extended_value is set with base_value = BaseValue::BASE_VALUE_WINNING_BID
  private_aggregation_value.mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_WINNING_BID);
  private_aggregation_value.mutable_extended_value()->set_scale(2.0f);
  private_aggregation_value.mutable_extended_value()->set_offset(1.0f);
  ASSERT_TRUE(private_aggregation_value.has_extended_value());
  ASSERT_THAT(HandlePrivateAggregationValuePostAuction(
                  base_values, private_aggregation_value),
              absl::OkStatus());
  ASSERT_EQ(private_aggregation_value.int_value(), 21);
  ASSERT_FALSE(private_aggregation_value.has_extended_value());

  // extended_value is set with base_value =
  // BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID
  private_aggregation_value.mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID);
  private_aggregation_value.mutable_extended_value()->set_scale(2.0f);
  private_aggregation_value.mutable_extended_value()->set_offset(1.0f);
  ASSERT_TRUE(private_aggregation_value.has_extended_value());
  ASSERT_THAT(HandlePrivateAggregationValuePostAuction(
                  base_values, private_aggregation_value),
              absl::OkStatus());
  ASSERT_EQ(private_aggregation_value.int_value(), 11);
  ASSERT_FALSE(private_aggregation_value.has_extended_value());

  // extended_value is set with base_value =
  // BaseValue::BASE_VALUE_BID_REJECTION_REASON
  private_aggregation_value.mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_BID_REJECTION_REASON);
  private_aggregation_value.mutable_extended_value()->set_scale(2.0f);
  private_aggregation_value.mutable_extended_value()->set_offset(1.0f);
  ASSERT_TRUE(private_aggregation_value.has_extended_value());
  ASSERT_THAT(HandlePrivateAggregationValuePostAuction(
                  base_values, private_aggregation_value),
              absl::OkStatus());
  ASSERT_EQ(private_aggregation_value.int_value(), 3);
  ASSERT_FALSE(private_aggregation_value.has_extended_value());
}

TEST(PrivateAggregationPostAuctionUtilTest,
     HandlePrivateAggregationValuePostAuctionUnsupportedBaseValue) {
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
  ASSERT_TRUE(private_aggregation_value.has_extended_value());
  ASSERT_THAT(
      HandlePrivateAggregationValuePostAuction(base_values,
                                               private_aggregation_value),
      absl::Status(absl::StatusCode::kInvalidArgument, kBaseValueNotSupported));
}

TEST(PrivateAggregationPostAuctionUtilTest,
     HandlePrivateAggregationValuePostAuctionInvalidValue) {
  PrivateAggregationValue private_aggregation_value;
  BaseValues base_values;
  base_values.winning_bid = 10.0f;
  base_values.highest_scoring_other_bid = 5.0f;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;
  ASSERT_THAT(HandlePrivateAggregationValuePostAuction(
                  base_values, private_aggregation_value),
              absl::Status(absl::StatusCode::kInvalidArgument,
                           kInvalidPrivateAggregationValueType));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
