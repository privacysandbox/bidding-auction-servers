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
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/strings/escaping.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/compression/gzip.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/error_accumulator.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kTestIgNameWin = "testIgNameWin";
constexpr absl::string_view kTestIgNameLoss = "testIgNameLoss";
constexpr absl::string_view kTestIgOwner = "testIgOwner";
constexpr absl::string_view kTestSeller = "kTestSeller";
constexpr int kPerAdtechPaapiContributionsLimit = 2;
constexpr int losing_ig_idx = 1;
constexpr int winning_ig_idx = 2;

using ::google::scp::core::test::EqualsProto;

absl::flat_hash_map<InterestGroupIdentity, int> GetInterestGroupIndexMap() {
  absl::flat_hash_map<InterestGroupIdentity, int> ig_idx_map;
  InterestGroupIdentity losing_ig = {
      .interest_group_owner = kTestIgOwner.data(),
      .interest_group_name = kTestIgNameLoss.data()};
  InterestGroupIdentity winning_ig = {
      .interest_group_owner = kTestIgOwner.data(),
      .interest_group_name = kTestIgNameWin.data()};
  ig_idx_map.try_emplace(losing_ig, losing_ig_idx);
  ig_idx_map.try_emplace(winning_ig, winning_ig_idx);
  return ig_idx_map;
}

TEST(HandlePrivateAggregationContributionsTest,
     FiltersAndPostProcessesContributions) {
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

  HandlePrivateAggregationContributions(GetInterestGroupIndexMap(), high_score,
                                        shared_buyer_bids_map);

  // Filling expected Bucket128Bit testFinalBucketFromSignal using values from
  // test util.
  auto ig_owner_it =
      high_score.ig_owner_highest_scoring_other_bids_map().find(kTestIgOwner);
  const auto& other_bids = ig_owner_it->second;
  float base_value_numerical_value = other_bids.values().Get(0).number_value();
  absl::uint128 offset_value =
      absl::MakeUint128(static_cast<uint64_t>(0), static_cast<uint64_t>(1));
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
  contribution0.set_ig_idx(winning_ig_idx);
  PrivateAggregateContribution contribution1;
  contribution1.mutable_value()->set_int_value(10);
  *contribution1.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFrom128Bit;
  contribution1.set_ig_idx(winning_ig_idx);
  PrivateAggregateContribution contribution2;
  contribution2.mutable_value()->set_int_value(11);
  *contribution2.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFromSignal;
  contribution2.set_ig_idx(losing_ig_idx);
  PrivateAggregateContribution contribution3;
  contribution3.mutable_value()->set_int_value(10);
  *contribution3.mutable_bucket()->mutable_bucket_128_bit() =
      testFinalBucketFrom128Bit;
  contribution3.set_ig_idx(losing_ig_idx);
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

TEST(GroupContributionsByAdTechTest, ReturnsMapOfAdTechAndContributions) {
  PrivateAggregateReportingResponse response1;
  PrivateAggregateContribution* contribution1 = response1.add_contributions();
  *contribution1 = GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  // This contribution is expected to be dropped since
  // per_adtech_paapi_contributions_limit is 2
  *response1.add_contributions() =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  *response1.add_contributions() =
      GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "");
  response1.set_adtech_origin(kTestIgOwner);
  PrivateAggregateReportingResponse response2;
  PrivateAggregateContribution* contribution2 = response2.add_contributions();
  *contribution2 = GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "");
  response2.set_adtech_origin(kTestSeller);

  PrivateAggregateReportingResponses responses;
  responses.Add(std::move(response1));
  responses.Add(std::move(response2));

  auto result =
      GroupContributionsByAdTech(kPerAdtechPaapiContributionsLimit, responses);

  ASSERT_EQ(result.size(), 2);
  ASSERT_EQ(result[kTestIgOwner].size(), 2);
  google::protobuf::util::MessageDifferencer differencer;
  ASSERT_TRUE(differencer.Compare(*result[kTestIgOwner][0], *contribution1));
  ASSERT_TRUE(differencer.Compare(*result[kTestIgOwner][1], *contribution1));
  ASSERT_EQ(result[kTestSeller].size(), 1);
  ASSERT_TRUE(differencer.Compare(*result[kTestSeller][0], *contribution2));
}

TEST(ConvertIntArrayToByteString, Converts128BitBucketToByteString) {
  Bucket128Bit bucket_128_bit;
  bucket_128_bit.add_bucket_128_bits(100);
  bucket_128_bit.add_bucket_128_bits(200);
  PrivateAggregationBucket private_aggregation_bucket;
  *private_aggregation_bucket.mutable_bucket_128_bit() = bucket_128_bit;
  int64_t expected_lower = absl::big_endian::FromHost64(100);
  int64_t expected_upper = absl::big_endian::FromHost64(200);
  std::string expected_result =
      std::string(reinterpret_cast<const char*>(&expected_upper), 8) +
      std::string(reinterpret_cast<const char*>(&expected_lower), 8);
  std::string byte_string =
      ConvertIntArrayToByteString(private_aggregation_bucket);
  EXPECT_EQ(byte_string, expected_result);
}

TEST(ConvertIntArrayToByteString, Converts64BitBucketToByteString) {
  Bucket128Bit bucket_128_bit;
  // Set default the first 64 bits
  bucket_128_bit.add_bucket_128_bits(100);
  PrivateAggregationBucket private_aggregation_bucket;
  *private_aggregation_bucket.mutable_bucket_128_bit() = bucket_128_bit;
  int64_t expected_lower = absl::big_endian::FromHost64(100);
  std::string expected_result =
      std::string(8, '\0') +
      std::string(reinterpret_cast<const char*>(&expected_lower), 8);
  std::string byte_string =
      ConvertIntArrayToByteString(private_aggregation_bucket);
  // buckey_128_bits has lower 64 bit number: 100 and upper 64 bit number: 0
  EXPECT_EQ(byte_string, expected_result);
}

TEST(HandlePrivateAggregationContributionsTest,
     NoContributionsProcessedWhenIgIdxNotFound) {
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

  HandlePrivateAggregationContributions({}, high_score, shared_buyer_bids_map);

  EXPECT_EQ(high_score.top_level_contributions().size(), 0);
}

TEST(CborSerializePAggContribution, SuccessfullySerializesBucketAndValue) {
  PrivateAggregateContribution contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumContributionKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result =
      CborSerializePAggContribution(contribution, err_handler, *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  absl::Span<struct cbor_pair> contribution_map(cbor_map_handle(cbor_internal),
                                                cbor_map_size(cbor_internal));
  ASSERT_EQ(contribution_map.size(), 2);
  ASSERT_TRUE(cbor_isa_string(contribution_map.at(0).key))
      << "Expected the key to be a string";
  EXPECT_EQ(kValue, CborDecodeString(contribution_map.at(0).key));
  EXPECT_EQ(contribution.value().int_value(),
            cbor_get_int(contribution_map.at(0).value));
  EXPECT_EQ(kBucket, CborDecodeString(contribution_map.at(1).key));
  EXPECT_EQ(ConvertIntArrayToByteString(contribution.bucket()),
            CborDecodeByteString(contribution_map.at(1).value));
}

TEST(CborSerializePAggContribution, SuccessfullySerializesEventContributions) {
  PrivateAggregateReportingResponse expected_response;
  expected_response.set_adtech_origin(kTestIgOwner);
  PrivateAggregateContribution contribution1 =
      GetTestContributionWithIntegers(EVENT_TYPE_CUSTOM, "clickEvent");
  PrivateAggregateContribution contribution0 =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  *expected_response.add_contributions() = contribution1;
  *expected_response.add_contributions() = contribution0;
  PrivateAggregateReportingResponses responses;
  responses.Add(std::move(expected_response));

  ContributionsPerAdTechMap contributions_per_adtech =
      GroupContributionsByAdTech(kPerAdtechPaapiContributionsLimit, responses);
  ASSERT_EQ(contributions_per_adtech.size(), 1);
  ASSERT_EQ(contributions_per_adtech[kTestIgOwner].size(), 2);
  ScopedCbor cbor_data_root(cbor_new_definite_map(1));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializePAggEventContributions(
      contributions_per_adtech[kTestIgOwner], err_handler, *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;
  absl::Span<struct cbor_pair> contribution_map(cbor_map_handle(cbor_internal),
                                                cbor_map_size(cbor_internal));
  ASSERT_EQ(contribution_map.size(), 1);
  ASSERT_TRUE(cbor_isa_string(contribution_map.at(0).key))
      << "Expected the key to be a string";
  EXPECT_EQ("eventContributions", CborDecodeString(contribution_map.at(0).key));
  ASSERT_TRUE(cbor_isa_array(contribution_map.at(0).value))
      << "Expected the value to be array";

  absl::StatusOr<std::vector<PrivateAggregateContribution>>
      decoded_event_contributions =
          CborDecodePAggEventContributions({}, *contribution_map.at(0).value);
  ASSERT_TRUE(decoded_event_contributions.ok());
  ASSERT_EQ((*decoded_event_contributions).size(), 2);
  google::protobuf::util::MessageDifferencer differencer;
  std::string diff_output;
  differencer.ReportDifferencesToString(&diff_output);
  // event is set only for custom events(not starting with reserved.)
  contribution0.clear_event();
  for (const auto& decoded_event_contribution : *decoded_event_contributions) {
    // The order of PrivateAggregateContributions does not have to be in
    //  the same order as in the list of PrivateAggregateContributions
    //  serialized.
    if (decoded_event_contribution.has_event()) {
      ASSERT_TRUE(
          differencer.Compare(decoded_event_contribution, contribution1))
          << diff_output;
    } else {
      ASSERT_TRUE(
          differencer.Compare(decoded_event_contribution, contribution0))
          << diff_output;
    }
  }
}

TEST(CborSerializePAggContribution, SuccessfullySerailizesIgContributions) {
  PrivateAggregateReportingResponse expected_response;
  expected_response.set_adtech_origin(kTestIgOwner);
  PrivateAggregateContribution contribution1 =
      GetTestContributionWithIntegers(EVENT_TYPE_CUSTOM, "clickEvent");
  PrivateAggregateContribution contribution0 =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  contribution1.set_ig_idx(1);
  *expected_response.add_contributions() = contribution1;
  *expected_response.add_contributions() = contribution0;
  PrivateAggregateReportingResponses responses;
  responses.Add()->CopyFrom(expected_response);
  ContributionsPerAdTechMap contributions_per_adtech =
      GroupContributionsByAdTech(kPerAdtechPaapiContributionsLimit, responses);
  ASSERT_EQ(contributions_per_adtech.size(), 1);
  ASSERT_EQ(contributions_per_adtech[kTestIgOwner].size(), 2);
  ScopedCbor cbor_data_root(cbor_new_definite_map(1));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializeIgContributions(
      contributions_per_adtech[kTestIgOwner], err_handler, *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;
  absl::Span<struct cbor_pair> contribution_map(cbor_map_handle(cbor_internal),
                                                cbor_map_size(cbor_internal));
  ASSERT_EQ(contribution_map.size(), 1);
  ASSERT_TRUE(cbor_isa_string(contribution_map.at(0).key))
      << "Expected the key to be a string";
  EXPECT_EQ(kIgContributions, CborDecodeString(contribution_map.at(0).key));
  ASSERT_TRUE(cbor_isa_array(contribution_map.at(0).value))
      << "Expected the value to be array";

  absl::StatusOr<std::vector<PrivateAggregateContribution>>
      decoded_ig_contributions =
          CborDecodePAggIgContributions(*contribution_map.at(0).value);
  ASSERT_TRUE(decoded_ig_contributions.ok());
  ASSERT_EQ((*decoded_ig_contributions).size(), 2);
  google::protobuf::util::MessageDifferencer differencer;
  std::string diff_output;
  differencer.ReportDifferencesToString(&diff_output);
  // event is set only for custom events(not starting with reserved.)
  contribution0.clear_event();
  for (const auto& decoded_event_contribution : *decoded_ig_contributions) {
    // The order of PrivateAggregateContributions does not have to be in
    //  the same order as in the list of PrivateAggregateContributions
    //  serialized.
    if (decoded_event_contribution.has_event()) {
      ASSERT_TRUE(
          differencer.Compare(decoded_event_contribution, contribution1))
          << diff_output;
    } else {
      ASSERT_TRUE(
          differencer.Compare(decoded_event_contribution, contribution0))
          << diff_output;
    }
  }
}

TEST(CborSerializePAggContribution, SuccessfullySerailizesPAggResponse) {
  int per_adtech_paapi_contributions_limit = 100;
  PrivateAggregateReportingResponse seller_pagg_response;
  PrivateAggregateReportingResponse buyer_pagg_response;
  PrivateAggregateContribution contribution1 =
      GetTestContributionWithIntegers(EVENT_TYPE_CUSTOM, "clickEvent");
  PrivateAggregateContribution contribution0 =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  contribution1.set_ig_idx(1);
  *buyer_pagg_response.add_contributions() = contribution1;
  *seller_pagg_response.add_contributions() = contribution0;
  buyer_pagg_response.set_adtech_origin(kTestIgOwner);
  seller_pagg_response.set_adtech_origin(kTestSeller);
  PrivateAggregateReportingResponses responses;
  responses.Add()->CopyFrom(seller_pagg_response);
  responses.Add()->CopyFrom(buyer_pagg_response);
  ScopedCbor cbor_data_root(cbor_new_definite_map(1));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result =
      CborSerializePAggResponse(responses, per_adtech_paapi_contributions_limit,
                                err_handler, *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;
  absl::Span<struct cbor_pair> contribution_map(cbor_map_handle(cbor_internal),
                                                cbor_map_size(cbor_internal));
  ASSERT_EQ(contribution_map.size(), 1);
  ASSERT_TRUE(cbor_isa_string(contribution_map.at(0).key))
      << "Expected the key to be a string";
  EXPECT_EQ(kPAggResponse, CborDecodeString(contribution_map.at(0).key));
  ASSERT_TRUE(cbor_isa_array(contribution_map.at(0).value))
      << "Expected the value to be array";

  absl::StatusOr<PrivateAggregateReportingResponses>
      decoded_adtech_contributions =
          CborDecodePAggResponse(*contribution_map.at(0).value);
  ASSERT_TRUE(decoded_adtech_contributions.ok());
  ASSERT_EQ((*decoded_adtech_contributions).size(), 2);
  google::protobuf::util::MessageDifferencer differencer;
  std::string diff_output;
  differencer.ReportDifferencesToString(&diff_output);
  for (const auto& paggResponse : *decoded_adtech_contributions) {
    if (paggResponse.adtech_origin() == kTestIgOwner) {
      ASSERT_TRUE(differencer.Compare(paggResponse, buyer_pagg_response))
          << diff_output;
    } else {
      // event is set only for custom events(not starting with reserved.)
      seller_pagg_response.mutable_contributions(0)->clear_event();
      ASSERT_TRUE(differencer.Compare(paggResponse, seller_pagg_response))
          << diff_output;
    }
  }
}

TEST(CborSerializePAggContribution,
     PAggResponseDecodeFailsWhenInputIsNotArray) {
  std::string bytes_string = absl::HexStringToBytes("A16361626363646566");
  std::vector<unsigned char> bytes(bytes_string.begin(), bytes_string.end());
  cbor_load_result cbor_result;
  cbor_item_t* loaded_data =
      cbor_load(bytes.data(), bytes.size(), &cbor_result);
  ScopedCbor root(loaded_data);
  absl::StatusOr<PrivateAggregateReportingResponses>
      decoded_adtech_contributions = CborDecodePAggResponse(*loaded_data);
  ASSERT_FALSE(decoded_adtech_contributions.ok());
}

TEST(HandlePrivateAggregationContributionsForGhostWinnersTest,
     FiltersAndPostProcessesContributions) {
  ScoreAdsResponse::AdScore ghost_winner_1;
  ghost_winner_1.set_interest_group_owner(kTestIgOwner);
  ghost_winner_1.set_interest_group_name(kTestIgNameWin);
  ghost_winner_1.set_buyer_bid(1.0);
  ScoreAdsResponse::AdScore ghost_winner_2;
  ghost_winner_2.set_interest_group_owner(kTestIgOwner);
  ghost_winner_2.set_interest_group_name(kTestIgNameLoss);
  ghost_winner_2.set_buyer_bid(1.0);

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

  // Using ScoreAdsRawResponse to mimic the flow in select_ad_reactor.
  ScoreAdsResponse::ScoreAdsRawResponse score_ads_raw_response;
  AdScores ghost_winning_scores;
  *ghost_winning_scores.Add() = std::move(ghost_winner_1);
  *ghost_winning_scores.Add() = std::move(ghost_winner_2);
  score_ads_raw_response.mutable_ghost_winning_ad_scores()->Swap(
      &ghost_winning_scores);

  for (auto& ghost_winning_ad_score :
       *score_ads_raw_response.mutable_ghost_winning_ad_scores()) {
    HandlePrivateAggregationContributionsForGhostWinner(
        GetInterestGroupIndexMap(), ghost_winning_ad_score,
        shared_buyer_bids_map);
  }

  PrivateAggregateReportingResponse expected_response_1;
  PrivateAggregateContribution contribution_1;
  contribution_1.mutable_value()->set_int_value(10);
  *contribution_1.mutable_bucket()->mutable_bucket_128_bit() =
      GetTestBucket128Bit();
  contribution_1.set_ig_idx(winning_ig_idx);
  expected_response_1.set_adtech_origin(kTestIgOwner);
  *expected_response_1.add_contributions() = std::move(contribution_1);
  ScoreAdsResponse::AdScore expected_ghost_winner_score_1;
  *expected_ghost_winner_score_1.add_top_level_contributions() =
      expected_response_1;
  expected_ghost_winner_score_1.set_interest_group_owner(kTestIgOwner);
  expected_ghost_winner_score_1.set_interest_group_name(kTestIgNameWin);
  expected_ghost_winner_score_1.set_buyer_bid(1.0);

  PrivateAggregateReportingResponse expected_response_2;
  expected_response_1.set_adtech_origin(kTestIgOwner);
  PrivateAggregateContribution contribution_2;
  contribution_2.mutable_value()->set_int_value(10);
  *contribution_2.mutable_bucket()->mutable_bucket_128_bit() =
      GetTestBucket128Bit();
  contribution_2.set_ig_idx(losing_ig_idx);
  expected_response_2.set_adtech_origin(kTestIgOwner);
  *expected_response_2.add_contributions() = std::move(contribution_2);
  ScoreAdsResponse::AdScore expected_ghost_winner_score_2;
  *expected_ghost_winner_score_2.add_top_level_contributions() =
      expected_response_2;
  expected_ghost_winner_score_2.set_interest_group_owner(kTestIgOwner);
  expected_ghost_winner_score_2.set_interest_group_name(kTestIgNameLoss);
  expected_ghost_winner_score_2.set_buyer_bid(1.0);

  EXPECT_THAT(score_ads_raw_response.ghost_winning_ad_scores()[0],
              EqualsProto(expected_ghost_winner_score_1));
  EXPECT_THAT(score_ads_raw_response.ghost_winning_ad_scores()[1],
              EqualsProto(expected_ghost_winner_score_2));
}
}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
