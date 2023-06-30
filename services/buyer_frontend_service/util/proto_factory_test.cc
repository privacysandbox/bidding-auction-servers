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

#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kSampleGenerationId[] = "sample-generation-id";
constexpr char kSampleAdtechDebugId[] = "sample-adtech-debug-id";
using ::google::protobuf::util::MessageDifferencer;
using GenBidsRawReq = GenerateBidsRequest::GenerateBidsRawRequest;
using GenBidsRawResp = GenerateBidsResponse::GenerateBidsRawResponse;

TEST(CreateGetBidsRawResponseTest, SetsAllBidsInGenerateBidsResponse) {
  auto input_raw_response = MakeARandomGenerateBidsRawResponse();
  auto ad_with_bid_low = MakeARandomAdWithBid(0, 10);
  auto ad_with_bid_high = MakeARandomAdWithBid(11, 20);
  input_raw_response.mutable_bids()->Clear();
  input_raw_response.mutable_bids()->Add()->CopyFrom(ad_with_bid_low);
  input_raw_response.mutable_bids()->Add()->CopyFrom(ad_with_bid_high);

  auto output = ProtoFactory::CreateGetBidsRawResponse(
      std::make_unique<GenBidsRawResp>(input_raw_response));

  EXPECT_EQ(output->bids().size(), 2);
  EXPECT_TRUE(
      MessageDifferencer::Equals(output->bids().at(0), ad_with_bid_low));
  EXPECT_TRUE(
      MessageDifferencer::Equals(output->bids().at(1), ad_with_bid_high));
}

TEST(CreateGetBidsRawResponseTest, ReturnsEmptyForNoAdsInGenerateBidsResponse) {
  auto input_raw_response = MakeARandomGenerateBidsRawResponse();
  input_raw_response.mutable_bids()->Clear();
  auto output = ProtoFactory::CreateGetBidsRawResponse(
      std::make_unique<GenBidsRawResp>(input_raw_response));

  EXPECT_TRUE(output->bids().empty());
}

TEST(CreateGetBidsRawResponseTest,
     ReturnsEmptyForMalformedGenerateBidsResponse) {
  auto output = ProtoFactory::CreateGetBidsRawResponse(
      std::make_unique<GenBidsRawResp>());

  EXPECT_TRUE(output->bids().empty());
}

TEST(CreateGenerateBidsRequestTest, SetsAllFieldsFromInputParamsForAndroid) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRawRequestForAndroid();
  auto bidding_signals = std::make_unique<BiddingSignals>();

  GetBidsRequest::GetBidsRawRequest input;
  // 1. Set Interest Group For Bidding
  for (auto& bidding_ca : expected_raw_output.interest_group_for_bidding()) {
    BuyerInput::InterestGroup ca = MakeARandomInterestGroupFromAndroid();
    ca.set_name(bidding_ca.name());

    ca.clear_ads();
    ca.clear_user_bidding_signals();
    if (bidding_ca.has_ads() && bidding_ca.ads().IsInitialized()) {
      ca.mutable_ads()->MergeFrom(bidding_ca.ads());
    }
    if (!bidding_ca.user_bidding_signals().empty()) {
      ca.set_user_bidding_signals(bidding_ca.user_bidding_signals());
    }

    if (!bidding_ca.ad_render_ids().empty()) {
      ca.mutable_ad_render_ids()->CopyFrom(bidding_ca.ad_render_ids());
    }
    if (!bidding_ca.ad_component_render_ids().empty()) {
      ca.mutable_component_ads()->CopyFrom(
          bidding_ca.ad_component_render_ids());
    }

    ca.clear_bidding_signals_keys();
    if (bidding_ca.trusted_bidding_signals_keys().size() > 0) {
      ca.mutable_bidding_signals_keys()->MergeFrom(
          bidding_ca.trusted_bidding_signals_keys());
    }

    // 5. Set Device Signals.
    if (bidding_ca.has_browser_signals() &&
        bidding_ca.browser_signals().IsInitialized()) {
      ca.mutable_browser_signals()->CopyFrom(bidding_ca.browser_signals());
      // wipe other field
      if (ca.has_android_signals()) {
        ca.clear_android_signals();
      }
    } else if (bidding_ca.has_android_signals()) {
      ca.mutable_android_signals()->CopyFrom(bidding_ca.android_signals());
      if (ca.has_browser_signals()) {
        ca.clear_browser_signals();
      }
    } else {
      if (ca.has_android_signals()) {
        ca.clear_android_signals();
      }
      if (ca.has_browser_signals()) {
        ca.clear_browser_signals();
      }
    }

    // Move Interest Group to Buyer Input
    *input.mutable_buyer_input()->add_interest_groups() = std::move(ca);
  }

  // 2. Set Auction Signals.
  input.set_auction_signals(expected_raw_output.auction_signals());
  // 3. Set Buyer Signals.
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  // 4. Set Bidding Signals
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(expected_raw_output.bidding_signals());

  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::move(bidding_signals), LogContext{});

  std::string difference;
  MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_TRUE(differencer.Compare(expected_raw_output, *raw_output))
      << difference;
}

TEST(CreateGenerateBidsRequestTest, SetsAllFieldsFromInputParamsForTestIG) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>();

  GetBidsRequest::GetBidsRawRequest input;

  // Create a test IG with ads.
  auto ig_with_two_ads = MakeAnInterestGroupSentFromDevice();

  // Check that IG parsed correctly.
  ASSERT_FALSE(ig_with_two_ads->name().empty());
  ASSERT_TRUE(ig_with_two_ads->has_browser_signals());
  ASSERT_TRUE(ig_with_two_ads->browser_signals().IsInitialized());
  ASSERT_TRUE(ig_with_two_ads->has_ads());
  ASSERT_TRUE(ig_with_two_ads->ads().IsInitialized());
  ASSERT_EQ(ig_with_two_ads->ads().values_size(), 2);
  ASSERT_GT(ig_with_two_ads->bidding_signals_keys_size(), 0);

  // Now transform the IG into the expected output IGForBidding and add it to
  // expected output object.
  expected_raw_output.mutable_interest_group_for_bidding()->Clear();

  GenBidsRawReq::InterestGroupForBidding* ig_for_bidding =
      expected_raw_output.mutable_interest_group_for_bidding()->Add();

  ig_for_bidding->set_name(ig_with_two_ads->name());
  ig_for_bidding->mutable_browser_signals()->CopyFrom(
      ig_with_two_ads->browser_signals());
  ig_for_bidding->mutable_ads()->MergeFrom(ig_with_two_ads->ads());
  ig_for_bidding->mutable_trusted_bidding_signals_keys()->MergeFrom(
      ig_with_two_ads->bidding_signals_keys());

  // Move Input Interest Group to Buyer Input.
  input.mutable_buyer_input()->mutable_interest_groups()->AddAllocated(
      ig_with_two_ads.release());
  // Check that exactly 1 IG is in the input.
  ASSERT_EQ(input.buyer_input().interest_groups().size(), 1);

  // 2. Set Auction Signals.
  input.set_auction_signals(expected_raw_output.auction_signals());
  // 3. Set Buyer Signals.
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  // 4. Set Bidding Signals
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(expected_raw_output.bidding_signals());

  input.set_seller(expected_raw_output.seller());
  input.set_publisher_name(expected_raw_output.publisher_name());

  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::move(bidding_signals), LogContext{});

  ASSERT_GT(expected_raw_output.interest_group_for_bidding().size(), 0);
  ASSERT_GT(raw_output->interest_group_for_bidding().size(), 0);

  EXPECT_TRUE(MessageDifferencer::Equals(expected_raw_output, *raw_output));

  if (!(MessageDifferencer::Equals(expected_raw_output, *raw_output))) {
    std::string expected_output_str, output_str;

    google::protobuf::util::MessageToJsonString(
        expected_raw_output.interest_group_for_bidding().at(0),
        &expected_output_str);
    google::protobuf::util::MessageToJsonString(
        raw_output->interest_group_for_bidding().at(0), &output_str);

    VLOG(0) << "\nExpected First IG:\n" << expected_output_str;
    VLOG(0) << "\nActual First IG:\n" << output_str;

    VLOG(0) << "\nExpected seller:\n" << expected_raw_output.seller();
    VLOG(0) << "\nActual seller:\n" << raw_output->seller();
  }
}

TEST(CreateGenerateBidsRequestTest, SetsEmptyBiddingSignalKeysForBrowserIG) {
  GetBidsRequest::GetBidsRawRequest input;

  // Create a test IG.
  BuyerInput::InterestGroup input_ig = MakeARandomInterestGroupFromBrowser();
  input_ig.mutable_bidding_signals_keys()->Clear();

  // Check that IG created correctly.
  ASSERT_EQ(input_ig.bidding_signals_keys_size(), 0);

  // Move Input Interest Group to Buyer Input.
  *input.mutable_buyer_input()->add_interest_groups() = std::move(input_ig);
  // Check that exactly 1 IG is in the input.
  ASSERT_EQ(input.buyer_input().interest_groups().size(), 1);

  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::make_unique<BiddingSignals>(),
      LogContext{});

  ASSERT_EQ(raw_output->interest_group_for_bidding().size(), 1);
  // Expect no bidding signal keys in output.
  EXPECT_EQ(raw_output->interest_group_for_bidding(0)
                .trusted_bidding_signals_keys()
                .size(),
            0);
}

TEST(CreateGenerateBidsRequestTest, SetsEmptyBiddingSignalKeysForAndroidIG) {
  GetBidsRequest input;

  // Create a test IG.
  BuyerInput::InterestGroup input_ig = MakeARandomInterestGroupFromAndroid();
  input_ig.mutable_bidding_signals_keys()->Clear();

  // Check that IG created correctly.
  ASSERT_EQ(input_ig.bidding_signals_keys_size(), 0);

  // Move Input Interest Group to Buyer Input.
  GetBidsRequest::GetBidsRawRequest get_bids_raw_request;
  *get_bids_raw_request.mutable_buyer_input()->add_interest_groups() =
      std::move(input_ig);
  // Check that exactly 1 IG is in the input.
  ASSERT_EQ(get_bids_raw_request.buyer_input().interest_groups().size(), 1);

  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      get_bids_raw_request, get_bids_raw_request.buyer_input(),
      std::make_unique<BiddingSignals>(), LogContext{});

  ASSERT_EQ(raw_output->interest_group_for_bidding().size(), 1);
  // Expect no bidding signal keys in output.
  EXPECT_EQ(raw_output->interest_group_for_bidding(0)
                .trusted_bidding_signals_keys()
                .size(),
            0);
}

TEST(CreateGenerateBidsRequestTest, SetsAllFieldsFromInputParamsForBrowser) {
  GenBidsRawReq expected_raw_output = privacy_sandbox::bidding_auction_servers::
      MakeARandomGenerateBidsRequestForBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>();

  GetBidsRequest::GetBidsRawRequest input;
  // 1. Set Interest Group For Bidding
  for (auto& bidding_ca : expected_raw_output.interest_group_for_bidding()) {
    BuyerInput::InterestGroup ca = MakeARandomInterestGroupFromBrowser();
    ca.set_name(bidding_ca.name());
    ca.clear_ads();
    ca.clear_user_bidding_signals();
    if (bidding_ca.has_ads() && bidding_ca.ads().IsInitialized()) {
      ca.mutable_ads()->MergeFrom(bidding_ca.ads());
    }
    if (!bidding_ca.user_bidding_signals().empty()) {
      ca.set_user_bidding_signals(bidding_ca.user_bidding_signals());
    }

    if (!bidding_ca.ad_render_ids().empty()) {
      ca.mutable_ad_render_ids()->CopyFrom(bidding_ca.ad_render_ids());
    }
    if (!bidding_ca.ad_component_render_ids().empty()) {
      ca.mutable_component_ads()->CopyFrom(
          bidding_ca.ad_component_render_ids());
    }

    ca.clear_bidding_signals_keys();
    if (bidding_ca.trusted_bidding_signals_keys().size() > 0) {
      ca.mutable_bidding_signals_keys()->MergeFrom(
          bidding_ca.trusted_bidding_signals_keys());
    }

    // 5. Set Device Signals.
    if (bidding_ca.has_browser_signals() &&
        bidding_ca.browser_signals().IsInitialized()) {
      ca.mutable_browser_signals()->CopyFrom(bidding_ca.browser_signals());
      // wipe other field
      if (ca.has_android_signals()) {
        ca.clear_android_signals();
      }
    } else if (bidding_ca.has_android_signals()) {
      ca.mutable_android_signals()->CopyFrom(bidding_ca.android_signals());
      if (ca.has_browser_signals()) {
        ca.clear_browser_signals();
      }
    } else {
      if (ca.has_android_signals()) {
        ca.clear_android_signals();
      }
      if (ca.has_browser_signals()) {
        ca.clear_browser_signals();
      }
    }

    // Move Interest Group to Buyer Input
    *input.mutable_buyer_input()->add_interest_groups() = std::move(ca);
  }

  // 2. Set Auction Signals.
  input.set_auction_signals(expected_raw_output.auction_signals());
  // 3. Set Buyer Signals.
  input.set_buyer_signals(expected_raw_output.buyer_signals());
  // 4. Set Bidding Signals
  bidding_signals->trusted_signals =
      std::make_unique<std::string>(expected_raw_output.bidding_signals());

  input.set_seller(expected_raw_output.seller());
  input.set_publisher_name(expected_raw_output.publisher_name());

  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::move(bidding_signals), LogContext{});

  EXPECT_TRUE(MessageDifferencer::Equals(expected_raw_output, *raw_output));

  std::string difference;
  MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  EXPECT_TRUE(differencer.Compare(expected_raw_output, *raw_output))
      << difference;

  if (!(differencer.Compare(expected_raw_output, *raw_output))) {
    std::string expected_output_str, output_str;

    google::protobuf::util::MessageToJsonString(
        expected_raw_output.interest_group_for_bidding().at(0),
        &expected_output_str);
    google::protobuf::util::MessageToJsonString(
        raw_output->interest_group_for_bidding().at(0), &output_str);

    VLOG(0) << "\nExpected First IG:\n" << expected_output_str;
    VLOG(0) << "\nActual First IG:\n" << output_str;

    VLOG(0) << "\nExpected seller:\n" << expected_raw_output.seller();
    VLOG(0) << "\nActual seller:\n" << raw_output->seller();

    VLOG(0) << "\Difference in comparison:\n" << difference;
  }
}

TEST(CreateGenerateBidsRequestTest,
     SetsEmptyBiddingSignalsForNullTrustedSignals) {
  auto bidding_signals = std::make_unique<BiddingSignals>();

  GetBidsRequest::GetBidsRawRequest input;
  LogContext log_context;
  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::move(bidding_signals), LogContext{});

  EXPECT_TRUE(raw_output->bidding_signals().empty());
}

TEST(CreateGenerateBidsRequestTest, SetsEnableEventLevelDebugReporting) {
  auto bidding_signals = std::make_unique<BiddingSignals>();

  GetBidsRequest::GetBidsRawRequest input;
  input.set_enable_debug_reporting(true);
  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::move(bidding_signals), LogContext{});

  EXPECT_TRUE(raw_output->enable_debug_reporting());
}

TEST(CreateGenerateBidsRequestTest, SetsLogContext) {
  auto bidding_signals = std::make_unique<BiddingSignals>();

  GetBidsRequest::GetBidsRawRequest input;
  LogContext log_context;
  log_context.set_generation_id(kSampleGenerationId);
  log_context.set_adtech_debug_id(kSampleAdtechDebugId);
  auto raw_output = ProtoFactory::CreateGenerateBidsRawRequest(
      input, input.buyer_input(), std::move(bidding_signals), log_context);

  EXPECT_EQ(raw_output->log_context().generation_id(),
            log_context.generation_id());
  EXPECT_EQ(raw_output->log_context().adtech_debug_id(),
            log_context.adtech_debug_id());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
