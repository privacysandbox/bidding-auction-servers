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

#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"

#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <utility>

#include "absl/numeric/int128.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/seller_frontend_service/data/scoring_signals.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kTestIgNameWin = "testIgNameWin";
constexpr absl::string_view kTestIgNameLoss = "testIgNameLoss";
constexpr absl::string_view kTestIgOwner = "testIgOwner";

TEST(HandlePrivateAggregationContributionsTest,
     DISABLED_FiltersAndPostProcessesContributions) {
  ScoreAdsResponse::AdScore high_score;
  high_score.set_interest_group_name(kTestIgNameWin);
  high_score.set_buyer_bid(1.0);
  HighestScoringOtherBidsMap ig_owner_highest_scoring_other_bids_map =
      GetHighestScoringOtherBidsMap(kTestIgOwner);
  high_score.mutable_ig_owner_highest_scoring_other_bids_map()->insert(
      ig_owner_highest_scoring_other_bids_map.begin(),
      ig_owner_highest_scoring_other_bids_map.end());

  BuyerBidsResponseMap shared_buyer_bids_map;
  GetBidsResponse::GetBidsRawResponse buyer_bids_raw_response =
      GetBidsRawResponseWithPrivateAggregationContributions(kTestIgNameWin,
                                                            kTestIgNameLoss);
  auto raw_response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  std::unique_ptr<GetBidsResponse::GetBidsRawResponse> buyer_bids_ptr =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>(
          buyer_bids_raw_response);

  shared_buyer_bids_map.try_emplace(
      static_cast<const std::basic_string<char>>(kTestIgOwner),
      std::move(buyer_bids_ptr));

  HandlePrivateAggregationContributions(high_score, shared_buyer_bids_map);

  // Filling expected Bucket128Bit testFinalBucketFromSignal using values from
  // test util.
  auto ig_owner_it =
      high_score.ig_owner_highest_scoring_other_bids_map().find(kTestIgOwner);
  const auto& other_bids = ig_owner_it->second;
  float base_value_numerical_value = other_bids.values().Get(0).number_value();
  absl::uint128 offset_value =
      absl::MakeUint128(static_cast<uint64_t>(112233445566778899ULL),
                        static_cast<uint64_t>(12345678901234567890ULL));
  absl::uint128 final_value = absl::MakeUint128(
      0, static_cast<uint64_t>(base_value_numerical_value * 2.0));
  final_value -= offset_value;
  Bucket128Bit testFinalBucketFromSignal;
  testFinalBucketFromSignal.add_bucket_128_bits(
      absl::Uint128Low64(final_value));
  testFinalBucketFromSignal.add_bucket_128_bits(
      absl::Uint128High64(final_value));
  Bucket128Bit testFinalBucketFrom128Bit = GetTestBucket128Bit();

  // Filling an expected AdScore object with values to match high_score after
  // HandlePrivateAggregationContributions is called.
  ScoreAdsResponse::AdScore expected_adscore;
  PrivateAggregateReportingResponse expected_response;
  expected_response.set_adtech_origin(kTestIgOwner);
  PrivateAggregateContribution contribution0;
  contribution0.mutable_value()->set_int_value(11);
  *contribution0.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFromSignal;
  PrivateAggregateContribution contribution1;
  contribution1.mutable_value()->set_int_value(10);
  *contribution1.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFrom128Bit;
  PrivateAggregateContribution contribution2;
  contribution2.mutable_value()->set_int_value(11);
  *contribution2.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFromSignal;
  PrivateAggregateContribution contribution3;
  contribution3.mutable_value()->set_int_value(10);
  *contribution3.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFrom128Bit;
  *expected_response.add_contributions() = std::move(contribution0);
  *expected_response.add_contributions() = std::move(contribution1);
  *expected_response.add_contributions() = std::move(contribution2);
  *expected_response.add_contributions() = std::move(contribution3);
  *expected_adscore.add_top_level_contributions() =
      std::move(expected_response);
  expected_adscore.set_interest_group_name(kTestIgNameWin);
  expected_adscore.set_buyer_bid(1.0);
  expected_adscore.mutable_ig_owner_highest_scoring_other_bids_map()->insert(
      ig_owner_highest_scoring_other_bids_map.begin(),
      ig_owner_highest_scoring_other_bids_map.end());

  google::protobuf::util::MessageDifferencer diff;
  std::string diff_output;
  diff.ReportDifferencesToString(&diff_output);
  EXPECT_TRUE(diff.Compare(high_score, expected_adscore)) << diff_output;
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
