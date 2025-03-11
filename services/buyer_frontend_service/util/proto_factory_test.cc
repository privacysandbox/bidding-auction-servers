// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/buyer_frontend_service/util/proto_factory.h"

#include "absl/log/check.h"
#include "api/bidding_auction_servers.pb.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/util/bidding_signals.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kSampleGenerationId[] = "sample-generation-id";
constexpr char kSampleAdtechDebugId[] = "sample-adtech-debug-id";
constexpr bool kIsConsentedDebug = true;
constexpr absl::string_view kConsentedDebugToken = "test";
constexpr uint32_t kTestDefaultDataVersion = 0;

using ::google::protobuf::util::MessageDifferencer;
using ::google::protobuf::util::MessageToJsonString;
using GenBidsRawReq = GenerateBidsRequest::GenerateBidsRawRequest;
using GenBidsRawResp = GenerateBidsResponse::GenerateBidsRawResponse;

PriorityVectorConfig GetDefaultPriorityVectorConfig() {
  rapidjson::Document priority_signals;
  return PriorityVectorConfig{.priority_vector_enabled = false,
                              .priority_signals = priority_signals,
                              .per_ig_priority_vectors = {}};
}

TEST(CreateGetBidsRawResponseTest, SetsAllBidsInGenerateBidsResponse) {
  auto input_raw_response = MakeARandomGenerateBidsRawResponse();
  auto ad_with_bid_low = MakeARandomAdWithBid(0, 10);
  auto ad_with_bid_high = MakeARandomAdWithBid(11, 20);
  input_raw_response.mutable_bids()->Clear();
  input_raw_response.mutable_bids()->Add()->CopyFrom(ad_with_bid_low);
  input_raw_response.mutable_bids()->Add()->CopyFrom(ad_with_bid_high);

  auto output = CreateGetBidsRawResponse(
      std::make_unique<GenBidsRawResp>(input_raw_response));

  EXPECT_EQ(output->bids_size(), 2);
  EXPECT_TRUE(
      MessageDifferencer::Equals(output->bids().at(0), ad_with_bid_low));
  EXPECT_TRUE(
      MessageDifferencer::Equals(output->bids().at(1), ad_with_bid_high));
}

TEST(CreateGetBidsRawResponseTest, ReturnsEmptyForNoAdsInGenerateBidsResponse) {
  auto input_raw_response = MakeARandomGenerateBidsRawResponse();
  input_raw_response.mutable_bids()->Clear();

  auto output = CreateGetBidsRawResponse(
      std::make_unique<GenBidsRawResp>(input_raw_response));

  EXPECT_TRUE(output->bids().empty());
}

TEST(CreateGetBidsRawResponseTest,
     ReturnsEmptyForMalformedGenerateBidsResponse) {
  auto output = CreateGetBidsRawResponse(std::make_unique<GenBidsRawResp>());

  EXPECT_TRUE(output->bids().empty());
}

TEST(CreateGenerateBidsRequestTest, SetsAllFieldsFromInputParamsForAndroid) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRawRequestForAndroid();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  // 1. Set Interest Group For Bidding
  for (const auto& bidding_ig :
       expected_raw_output.interest_group_for_bidding()) {
    auto input_ig =
        std::make_unique<BuyerInputForBidding::InterestGroupForBidding>();
    input_ig->set_name(bidding_ig.name());

    input_ig->set_user_bidding_signals(bidding_ig.user_bidding_signals());
    input_ig->mutable_ad_render_ids()->CopyFrom(bidding_ig.ad_render_ids());
    input_ig->mutable_component_ads()->CopyFrom(
        bidding_ig.ad_component_render_ids());
    input_ig->mutable_bidding_signals_keys()->MergeFrom(
        bidding_ig.trusted_bidding_signals_keys());

    // 5. Set Device Signals.
    if (bidding_ig.has_browser_signals_for_bidding() &&
        bidding_ig.browser_signals_for_bidding().IsInitialized()) {
      input_ig->mutable_browser_signals()->CopyFrom(
          bidding_ig.browser_signals_for_bidding());
    } else if (bidding_ig.has_android_signals_for_bidding()) {
      input_ig->mutable_android_signals()->CopyFrom(
          bidding_ig.android_signals_for_bidding());
    }

    // Move Interest Group to Buyer Input
    input.mutable_buyer_input_for_bidding()
        ->mutable_interest_groups()
        ->AddAllocated(input_ig.release());
  }
  // 2. Set Auction Signals.
  input.set_auction_signals(expected_raw_output.auction_signals());
  // 3. Set Buyer Signals.
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  // 11. Set Multi Bid Limit.
  input.set_enforce_kanon(expected_raw_output.enforce_kanon());
  input.set_multi_bid_limit(expected_raw_output.multi_bid_limit());
  input.mutable_blob_versions()->CopyFrom(expected_raw_output.blob_versions());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size,
          expected_raw_output.data_version(), GetDefaultPriorityVectorConfig(),
          {.enable_kanon = true})
          .raw_request;
  std::string difference;
  MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_TRUE(differencer.Compare(expected_raw_output, *raw_output))
      << difference;
}

TEST(CreateGenerateBidsRequestTest, SetsAllFieldsFromInputParamsForBrowser) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  // 1. Set Interest Group For Bidding
  for (const auto& bidding_ig :
       expected_raw_output.interest_group_for_bidding()) {
    auto input_ig =
        std::make_unique<BuyerInputForBidding::InterestGroupForBidding>();
    input_ig->set_name(bidding_ig.name());
    input_ig->set_user_bidding_signals(bidding_ig.user_bidding_signals());
    input_ig->mutable_ad_render_ids()->CopyFrom(bidding_ig.ad_render_ids());
    input_ig->mutable_component_ads()->CopyFrom(
        bidding_ig.ad_component_render_ids());
    input_ig->mutable_bidding_signals_keys()->MergeFrom(
        bidding_ig.trusted_bidding_signals_keys());

    // 5. Set Device Signals.
    if (bidding_ig.has_browser_signals_for_bidding() &&
        bidding_ig.browser_signals_for_bidding().IsInitialized()) {
      input_ig->mutable_browser_signals()->CopyFrom(
          bidding_ig.browser_signals_for_bidding());
    } else if (bidding_ig.has_android_signals_for_bidding()) {
      input_ig->mutable_android_signals()->CopyFrom(
          bidding_ig.android_signals_for_bidding());
    }

    // Move Interest Group to Buyer Input
    input.mutable_buyer_input_for_bidding()
        ->mutable_interest_groups()
        ->AddAllocated(input_ig.release());
  }

  // 2. Set Auction Signals.
  input.set_auction_signals(expected_raw_output.auction_signals());
  // 3. Set Buyer Signals.
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  input.set_seller(expected_raw_output.seller());
  input.set_publisher_name(expected_raw_output.publisher_name());
  // 11. Set Multi Bid Limit.
  input.set_enforce_kanon(expected_raw_output.enforce_kanon());
  input.set_multi_bid_limit(expected_raw_output.multi_bid_limit());
  input.mutable_blob_versions()->CopyFrom(expected_raw_output.blob_versions());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size,
          expected_raw_output.data_version(), GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_TRUE(MessageDifferencer::Equals(expected_raw_output, *raw_output));

  std::string difference;
  MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_TRUE(differencer.Compare(expected_raw_output, *raw_output))
      << difference;

  if (!(differencer.Compare(expected_raw_output, *raw_output))) {
    std::string expected_output_str, output_str;

    CHECK_OK(MessageToJsonString(
        expected_raw_output.interest_group_for_bidding().at(0),
        &expected_output_str));
    CHECK_OK(MessageToJsonString(raw_output->interest_group_for_bidding().at(0),
                                 &output_str));

    ABSL_LOG(INFO) << "\nExpected First IG:\n" << expected_output_str;
    ABSL_LOG(INFO) << "\nActual First IG:\n" << output_str;

    ABSL_LOG(INFO) << "\nExpected seller:\n" << expected_raw_output.seller();
    ABSL_LOG(INFO) << "\nActual seller:\n" << raw_output->seller();

    ABSL_LOG(INFO) << "\nDifference in comparison:\n" << difference;
  }
}

TEST(CreateGenerateBidsRequestTest, SetsAllFieldsFromInputParamsForTestIG) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  GetBidsRequest::GetBidsRawRequest input;

  // Create a test IG with ads.
  auto ig_with_two_ads = MakeAnInterestGroupSentFromDevice();
  // Check that IG parsed correctly.
  ASSERT_FALSE(ig_with_two_ads->name().empty());
  ASSERT_TRUE(ig_with_two_ads->has_browser_signals());
  ASSERT_TRUE(ig_with_two_ads->browser_signals().IsInitialized());
  ASSERT_EQ(ig_with_two_ads->ad_render_ids_size(), 2);
  ASSERT_GT(ig_with_two_ads->bidding_signals_keys_size(), 0);

  // Get bidding signals for test IG
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      MakeBiddingSignalsForIGFromDevice(*ig_with_two_ads.get()));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  // Now transform the IG into the expected output IGForBidding and add it to
  // expected output object.
  expected_raw_output.mutable_interest_group_for_bidding()->Clear();
  GenBidsRawReq::InterestGroupForBidding* ig_for_bidding =
      expected_raw_output.mutable_interest_group_for_bidding()->Add();
  ig_for_bidding->set_name(ig_with_two_ads->name());
  ig_for_bidding->mutable_browser_signals_for_bidding()->CopyFrom(
      ToBrowserSignalsForBidding(ig_with_two_ads->browser_signals()));
  ig_for_bidding->mutable_ad_render_ids()->MergeFrom(
      ig_with_two_ads->ad_render_ids());
  ig_for_bidding->mutable_trusted_bidding_signals_keys()->MergeFrom(
      ig_with_two_ads->bidding_signals_keys());
  ig_for_bidding->set_trusted_bidding_signals(
      MakeTrustedBiddingSignalsForIG(*ig_for_bidding));

  // Move Input Interest Group to Buyer Input.
  *input.mutable_buyer_input_for_bidding()->mutable_interest_groups()->Add() =
      ToInterestGroupForBidding(*ig_with_two_ads.get());
  // Check that exactly 1 IG is in the input.
  ASSERT_EQ(input.buyer_input_for_bidding().interest_groups_size(), 1);

  // 2. Set Auction Signals.
  input.set_auction_signals(expected_raw_output.auction_signals());
  // 3. Set Buyer Signals.
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  input.set_seller(expected_raw_output.seller());
  input.set_publisher_name(expected_raw_output.publisher_name());
  // 11. Set Multi Bid Limit.
  input.set_enforce_kanon(expected_raw_output.enforce_kanon());
  input.set_multi_bid_limit(expected_raw_output.multi_bid_limit());
  input.mutable_blob_versions()->CopyFrom(expected_raw_output.blob_versions());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size,
          expected_raw_output.data_version(), GetDefaultPriorityVectorConfig())
          .raw_request;
  ASSERT_GT(expected_raw_output.interest_group_for_bidding_size(), 0);
  ASSERT_GT(raw_output->interest_group_for_bidding_size(), 0);

  EXPECT_TRUE(MessageDifferencer::Equals(expected_raw_output, *raw_output));

  if (!(MessageDifferencer::Equals(expected_raw_output, *raw_output))) {
    std::string expected_output_str, output_str;

    CHECK_OK(MessageToJsonString(
        expected_raw_output.interest_group_for_bidding().at(0),
        &expected_output_str));
    CHECK_OK(MessageToJsonString(raw_output->interest_group_for_bidding().at(0),
                                 &output_str));

    ABSL_LOG(INFO) << "\nExpected First IG:\n" << expected_output_str;
    ABSL_LOG(INFO) << "\nActual First IG:\n" << output_str;

    ABSL_LOG(INFO) << "\nExpected seller:\n" << expected_raw_output.seller();
    ABSL_LOG(INFO) << "\nActual seller:\n" << raw_output->seller();
  }
}

TEST(CreateGenerateBidsRequestTest, SkipsIGWithoutBiddingSignalsKeys) {
  GetBidsRequest::GetBidsRawRequest input;
  auto input_ig = MakeARandomInterestGroupFromBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      MakeBiddingSignalsForIGFromDevice(*input_ig.get()));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  input_ig->mutable_bidding_signals_keys()->Clear();
  ASSERT_EQ(input_ig->bidding_signals_keys_size(), 0);
  input.mutable_buyer_input()->mutable_interest_groups()->AddAllocated(
      input_ig.release());
  ASSERT_EQ(input.buyer_input().interest_groups_size(), 1);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_EQ(raw_output->interest_group_for_bidding_size(), 0);
}

TEST(CreateGenerateBidsRequestTest,
     AllowsIGWithoutBiddingSignalsKeysWhenNotRequired) {
  GetBidsRequest::GetBidsRawRequest input;
  auto input_ig = MakeARandomInterestGroupFromBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      MakeBiddingSignalsForIGFromDevice(*input_ig.get()));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  input_ig->mutable_bidding_signals_keys()->Clear();
  ASSERT_EQ(input_ig->bidding_signals_keys_size(), 0);
  *input.mutable_buyer_input_for_bidding()->mutable_interest_groups()->Add() =
      ToInterestGroupForBidding(*input_ig.get());
  ASSERT_EQ(input.buyer_input_for_bidding().interest_groups_size(), 1);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig(), {.require_bidding_signals = false})
          .raw_request;
  ASSERT_EQ(raw_output->interest_group_for_bidding_size(), 1);
  EXPECT_EQ(raw_output->interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            0);
  EXPECT_EQ(raw_output->interest_group_for_bidding(0).trusted_bidding_signals(),
            kNullBiddingSignalsJson);
}

constexpr char kTestBiddingSignals[] =
    R"JSON({"keys":{"key1":["val1"]}, "perInterestGroupData":{"ig":["val1"]}})JSON";
constexpr char kTestTrustedBiddingSignals[] = R"JSON({"key1":["val1"]})JSON";

TEST(CreateGenerateBidsRequestTest, HandlesIGWithDuplicateBiddingSignalsKeys) {
  GetBidsRequest::GetBidsRawRequest input;
  auto input_ig = MakeARandomInterestGroupFromBrowser();
  input_ig->set_name("ig");
  input_ig->mutable_bidding_signals_keys()->Clear();
  input_ig->add_bidding_signals_keys("key1");
  ASSERT_EQ(input_ig->bidding_signals_keys_size(), 1);
  input_ig->add_bidding_signals_keys("key1");
  ASSERT_EQ(input_ig->bidding_signals_keys_size(), 2);
  *input.mutable_buyer_input_for_bidding()->mutable_interest_groups()->Add() =
      ToInterestGroupForBidding(*input_ig.get());
  ASSERT_EQ(input.buyer_input_for_bidding().interest_groups_size(), 1);

  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(kTestBiddingSignals);
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_EQ(raw_output->interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            1);
  EXPECT_EQ(raw_output->interest_group_for_bidding(0).trusted_bidding_signals(),
            kTestTrustedBiddingSignals);
}

TEST(CreateGenerateBidsRequestTest,
     HandlesIGWithBiddingSignalsKeysWithoutValue) {
  GetBidsRequest::GetBidsRawRequest input;
  auto input_ig = MakeARandomInterestGroupFromBrowser();
  input_ig->set_name("ig");
  input_ig->mutable_bidding_signals_keys()->Clear();
  input_ig->add_bidding_signals_keys("key1");
  input_ig->add_bidding_signals_keys("key2");
  ASSERT_EQ(input_ig->bidding_signals_keys_size(), 2);
  *input.mutable_buyer_input_for_bidding()->mutable_interest_groups()->Add() =
      ToInterestGroupForBidding(*input_ig.get());
  ASSERT_EQ(input.buyer_input_for_bidding().interest_groups_size(), 1);

  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(kTestBiddingSignals);
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_EQ(raw_output->interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            1);
  EXPECT_EQ(raw_output->interest_group_for_bidding(0).trusted_bidding_signals(),
            kTestTrustedBiddingSignals);
}

GetBidsRequest::GetBidsRawRequest MakeGetBidsRawRequestWithoutBiddingSignals() {
  GetBidsRequest::GetBidsRawRequest input;
  auto input_ig = MakeARandomInterestGroupFromBrowser();
  input_ig->set_name("ig");
  input_ig->mutable_bidding_signals_keys()->Clear();
  input_ig->add_bidding_signals_keys("key2");
  *input.mutable_buyer_input_for_bidding()->mutable_interest_groups()->Add() =
      ToInterestGroupForBidding(*input_ig.get());
  return input;
}

TEST(CreateGenerateBidsRequestTest, SkipsIGWithoutBiddingSignals) {
  GetBidsRequest::GetBidsRawRequest input =
      MakeGetBidsRawRequestWithoutBiddingSignals();

  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(kTestBiddingSignals);
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  // IG is expected to be filtered because it has no bidding signals but they
  // are required by default (bidding_signals_fetch_mode flag = "REQUIRED").
  ASSERT_EQ(raw_output->interest_group_for_bidding_size(), 0);
}

TEST(CreateGenerateBidsRequestTest,
     AllowsIGWithoutBiddingSignalsWhenNotRequired) {
  GetBidsRequest::GetBidsRawRequest input =
      MakeGetBidsRawRequestWithoutBiddingSignals();

  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(kTestBiddingSignals);
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig(), {.require_bidding_signals = false})
          .raw_request;
  ASSERT_EQ(raw_output->interest_group_for_bidding_size(), 1);
  EXPECT_EQ(raw_output->interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            0);
  EXPECT_EQ(raw_output->interest_group_for_bidding(0).trusted_bidding_signals(),
            kNullBiddingSignalsJson);
}

TEST(CreateGenerateBidsRequestTest, SetsEnableEventLevelDebugReporting) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  input.set_enable_debug_reporting(true);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_TRUE(raw_output->enable_debug_reporting());
}

TEST(CreateGenerateBidsRequestTest, SetsLogContext) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  input.mutable_log_context()->set_generation_id(kSampleGenerationId);
  input.mutable_log_context()->set_adtech_debug_id(kSampleAdtechDebugId);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_EQ(raw_output->log_context().generation_id(),
            input.log_context().generation_id());
  EXPECT_EQ(raw_output->log_context().adtech_debug_id(),
            input.log_context().adtech_debug_id());
}

TEST(CreateGenerateBidsRequestTest, SetsConsentedDebugConfig) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  auto* consented_debug_config = input.mutable_consented_debug_config();
  consented_debug_config->set_is_consented(kIsConsentedDebug);
  consented_debug_config->set_token(kConsentedDebugToken);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_EQ(raw_output->consented_debug_config().is_consented(),
            kIsConsentedDebug);
  EXPECT_EQ(raw_output->consented_debug_config().token(), kConsentedDebugToken);
}

TEST(CreateGenerateBidsRequestTest, SetsTopLevelSellerForComponentAuction) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  input.set_top_level_seller(MakeARandomString());

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig())
          .raw_request;
  EXPECT_EQ(input.top_level_seller(), raw_output->top_level_seller());
}

TEST(CreateGenerateBidsRequestTest, SetsMultiBidLimit) {
  GenBidsRawReq expected_raw_output = MakeARandomGenerateBidsRequestForBrowser(
      /*enforce_kanon=*/true, /*multi_bid_limit=*/5);
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  input.set_enforce_kanon(true);
  input.set_multi_bid_limit(5);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig(), {.enable_kanon = true})
          .raw_request;
  ASSERT_EQ(raw_output->enforce_kanon(), expected_raw_output.enforce_kanon());
  EXPECT_EQ(raw_output->multi_bid_limit(),
            expected_raw_output.multi_bid_limit());
}

TEST(CreateGenerateBidsRequestTest, MultiBidLimitDefaultsWithFalseFlags) {
  GenBidsRawReq expected_raw_output =
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  input.set_enforce_kanon(false);

  auto raw_output =
      PrepareGenerateBidsRequest(
          input, std::move(parsed_bidding_signals->bidding_signals),
          (*parsed_bidding_signals).raw_size, kTestDefaultDataVersion,
          GetDefaultPriorityVectorConfig(), {.enable_kanon = false})
          .raw_request;
  ASSERT_FALSE(raw_output->enforce_kanon());
  EXPECT_EQ(raw_output->multi_bid_limit(), kDefaultMultiBidLimit);
}

TEST(CreateGenerateBidsRequestTest, VerifyPriorityVectorFiltering) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRawRequestForAndroid(false, kDefaultMultiBidLimit,
                                                  2);
  auto bidding_signals = std::make_unique<BiddingSignals>(
      GetBiddingSignalsFromGenerateBidsRequest(expected_raw_output));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());

  GetBidsRequest::GetBidsRawRequest input;
  for (const auto& bidding_ig :
       expected_raw_output.interest_group_for_bidding()) {
    auto input_ig =
        std::make_unique<BuyerInputForBidding::InterestGroupForBidding>();
    input_ig->set_name(bidding_ig.name());
    input_ig->set_user_bidding_signals(bidding_ig.user_bidding_signals());
    input_ig->mutable_ad_render_ids()->CopyFrom(bidding_ig.ad_render_ids());
    input_ig->mutable_component_ads()->CopyFrom(
        bidding_ig.ad_component_render_ids());
    input_ig->mutable_bidding_signals_keys()->MergeFrom(
        bidding_ig.trusted_bidding_signals_keys());
    input_ig->mutable_android_signals()->CopyFrom(bidding_ig.android_signals());
    input.mutable_buyer_input_for_bidding()
        ->mutable_interest_groups()
        ->AddAllocated(input_ig.release());
  }

  input.set_auction_signals(expected_raw_output.auction_signals());
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  input.set_enforce_kanon(expected_raw_output.enforce_kanon());
  input.set_multi_bid_limit(expected_raw_output.multi_bid_limit());

  rapidjson::Document priority_signals(rapidjson::kObjectType);
  priority_signals.AddMember("a", 1, priority_signals.GetAllocator());

  absl::flat_hash_map<std::string, rapidjson::Value> per_ig_priority_vectors;

  // IG 1 - will have a priority less than zero and get filtered out.
  rapidjson::Document ig_1_priority_vector_doc;
  rapidjson::Value ig_1_priority_vector(rapidjson::kObjectType);
  ig_1_priority_vector.AddMember("a", -1,
                                 ig_1_priority_vector_doc.GetAllocator());
  per_ig_priority_vectors[expected_raw_output.interest_group_for_bidding()[0]
                              .name()] = ig_1_priority_vector;

  // IG 2 - will have a priority greater than zero and won't be filtered out.
  rapidjson::Document ig_2_priority_vector_doc;
  rapidjson::Value ig_2_priority_vector(rapidjson::kObjectType);
  ig_2_priority_vector.AddMember("a", 1,
                                 ig_2_priority_vector_doc.GetAllocator());
  per_ig_priority_vectors[expected_raw_output.interest_group_for_bidding()[1]
                              .name()] = ig_2_priority_vector;

  PriorityVectorConfig pv_config = {
      .priority_vector_enabled = true,
      .priority_signals = priority_signals,
      .per_ig_priority_vectors = per_ig_priority_vectors};
  auto result = PrepareGenerateBidsRequest(
      input, std::move(parsed_bidding_signals->bidding_signals),
      (*parsed_bidding_signals).raw_size, expected_raw_output.data_version(),
      pv_config, {.enable_kanon = true});
  auto raw_output = std::move(result.raw_request);
  EXPECT_EQ(raw_output->interest_group_for_bidding().size(), 1);
  EXPECT_EQ(raw_output->interest_group_for_bidding()[0].name(),
            expected_raw_output.interest_group_for_bidding()[1].name());
  EXPECT_EQ(result.percent_igs_filtered, .50);
}

TEST(CreateGenerateProtectedAppSignalsBidsRawRequestTest,
     SetsAppropriateFields) {
  auto get_bids_request = CreateGetBidsRawRequest();
  auto request = CreateGenerateProtectedAppSignalsBidsRawRequest(
      get_bids_request, /*enable_kanon=*/true);

  EXPECT_EQ(request->auction_signals(), kTestAuctionSignals);
  EXPECT_EQ(request->buyer_signals(), kTestBuyerSignals);
  EXPECT_EQ(request->protected_app_signals().encoding_version(),
            kTestEncodingVersion);
  EXPECT_EQ(request->protected_app_signals().app_install_signals(),
            kTestProtectedAppSignals);
  EXPECT_EQ(request->seller(), kTestSeller);
  EXPECT_EQ(request->publisher_name(), kTestPublisherName);
  EXPECT_TRUE(request->top_level_seller().empty());
  EXPECT_TRUE(request->enable_debug_reporting());
  EXPECT_TRUE(request->enable_unlimited_egress());
  ASSERT_TRUE(request->enforce_kanon());
  EXPECT_EQ(request->multi_bid_limit(), kTestMultiBidLimit);
  EXPECT_EQ(request->log_context().generation_id(), kTestGenerationId);
  EXPECT_EQ(request->log_context().adtech_debug_id(), kTestAdTechDebugId);
  EXPECT_TRUE(request->consented_debug_config().is_consented());
  EXPECT_EQ(request->consented_debug_config().token(),
            kTestConsentedDebuggingToken);
  ASSERT_TRUE(request->has_contextual_protected_app_signals_data());
  ASSERT_EQ(
      request->contextual_protected_app_signals_data().ad_render_ids_size(), 1);
  EXPECT_EQ(
      request->contextual_protected_app_signals_data().ad_render_ids().at(0),
      kTestContextualPasAdRenderId);
}

TEST(CreateGenerateProtectedAppSignalsBidsRawRequestTest,
     SetsAppropriateFieldsForComponentAuction) {
  auto get_bids_request = CreateGetBidsRawRequest();
  get_bids_request.set_top_level_seller(kTestSeller);

  auto request = CreateGenerateProtectedAppSignalsBidsRawRequest(
      get_bids_request, /*enable_kanon=*/true);

  EXPECT_EQ(request->auction_signals(), kTestAuctionSignals);
  EXPECT_EQ(request->buyer_signals(), kTestBuyerSignals);
  EXPECT_EQ(request->protected_app_signals().encoding_version(),
            kTestEncodingVersion);
  EXPECT_EQ(request->protected_app_signals().app_install_signals(),
            kTestProtectedAppSignals);
  EXPECT_EQ(request->seller(), kTestSeller);
  EXPECT_EQ(request->publisher_name(), kTestPublisherName);
  EXPECT_EQ(request->top_level_seller(), kTestSeller);
  EXPECT_TRUE(request->enable_debug_reporting());
  EXPECT_TRUE(request->enable_unlimited_egress());
  ASSERT_TRUE(request->enforce_kanon());
  EXPECT_EQ(request->multi_bid_limit(), kTestMultiBidLimit);
  EXPECT_EQ(request->log_context().generation_id(), kTestGenerationId);
  EXPECT_EQ(request->log_context().adtech_debug_id(), kTestAdTechDebugId);
  EXPECT_TRUE(request->consented_debug_config().is_consented());
  EXPECT_EQ(request->consented_debug_config().token(),
            kTestConsentedDebuggingToken);
  ASSERT_TRUE(request->has_contextual_protected_app_signals_data());
  ASSERT_EQ(
      request->contextual_protected_app_signals_data().ad_render_ids_size(), 1);
  EXPECT_EQ(
      request->contextual_protected_app_signals_data().ad_render_ids().at(0),
      kTestContextualPasAdRenderId);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
