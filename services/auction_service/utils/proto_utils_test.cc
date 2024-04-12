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

#include "services/auction_service/utils/proto_utils.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/common/test/random.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestPublisher[] = "publisher.com";
constexpr char kTestIGOwner[] = "interest_group_owner";
constexpr char kTestRenderUrl[] = "https://render_url";
constexpr char kTestComponentSeller[] = "component_seller_origin";
constexpr char kTestTopLevelSeller[] = "top_level_seller_origin";
constexpr char kTestAdComponentUrl_1[] = "https://compoennt_1";
constexpr char kTestAdComponentUrl_2[] = "https://component_2";
constexpr char kTestBidCurrency[] = "ABC";

google::protobuf::RepeatedPtrField<std::string> MakeMockAdComponentUrls() {
  google::protobuf::RepeatedPtrField<std::string> component_urls;
  component_urls.Add(kTestAdComponentUrl_1);
  component_urls.Add(kTestAdComponentUrl_2);
  return component_urls;
}

rapidjson::Document ParseJson(absl::string_view output) {
  rapidjson::Document parsed_output;
  rapidjson::ParseResult parsed_result =
      parsed_output.Parse<rapidjson::kParseFullPrecisionFlag>(output.data());
  EXPECT_FALSE(parsed_result.IsError()) << parsed_result.Code();
  return parsed_output;
}

TEST(MakeBidMetadataForTopLevelAuctionTest, PopulatesExpectedValues) {
  std::string output = MakeBidMetadataForTopLevelAuction(
      kTestPublisher, kTestIGOwner, kTestRenderUrl, MakeMockAdComponentUrls(),
      kTestComponentSeller, kTestBidCurrency);

  auto parsed_output = ParseJson(output);
  EXPECT_EQ(parsed_output["topWindowHostname"], kTestPublisher);
  EXPECT_EQ(parsed_output["interestGroupOwner"], kTestIGOwner);
  EXPECT_EQ(parsed_output["renderUrl"], kTestRenderUrl);
  EXPECT_EQ(parsed_output["componentSeller"], kTestComponentSeller);
  ASSERT_TRUE(parsed_output["adComponents"].IsArray());
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[0], kTestAdComponentUrl_1);
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[1], kTestAdComponentUrl_2);
  EXPECT_EQ(parsed_output["bidCurrency"], kTestBidCurrency);
}

TEST(MakeBidMetadataTest, PopulatesExpectedValues) {
  std::string output =
      MakeBidMetadata(kTestPublisher, kTestIGOwner, kTestRenderUrl,
                      MakeMockAdComponentUrls(), "", kTestBidCurrency);

  auto parsed_output = ParseJson(output);
  EXPECT_EQ(parsed_output["topWindowHostname"], kTestPublisher);
  EXPECT_EQ(parsed_output["interestGroupOwner"], kTestIGOwner);
  EXPECT_EQ(parsed_output["renderUrl"], kTestRenderUrl);
  ASSERT_TRUE(parsed_output["adComponents"].IsArray());
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[0], kTestAdComponentUrl_1);
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[1], kTestAdComponentUrl_2);
  EXPECT_EQ(parsed_output["bidCurrency"], kTestBidCurrency);
}

TEST(MakeBidMetadataTest, PopulatesExpectedValuesForComponentAuction) {
  std::string output = MakeBidMetadata(
      kTestPublisher, kTestIGOwner, kTestRenderUrl, MakeMockAdComponentUrls(),
      kTestTopLevelSeller, kTestBidCurrency);

  auto parsed_output = ParseJson(output);
  EXPECT_EQ(parsed_output["topWindowHostname"], kTestPublisher);
  EXPECT_EQ(parsed_output["interestGroupOwner"], kTestIGOwner);
  EXPECT_EQ(parsed_output["renderUrl"], kTestRenderUrl);
  ASSERT_TRUE(parsed_output["adComponents"].IsArray());
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[0], kTestAdComponentUrl_1);
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[1], kTestAdComponentUrl_2);
  EXPECT_EQ(parsed_output["topLevelSeller"], kTestTopLevelSeller);
  EXPECT_EQ(parsed_output["bidCurrency"], kTestBidCurrency);
}

using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using google::scp::core::test::EqualsProto;

TEST(MapAuctionResultToAdWithBidMetadataTest, PopulatesExpectedValues) {
  AuctionResult auction_result = MakeARandomComponentAuctionResult(
      MakeARandomString(), kTestTopLevelSeller);

  // copy since this will be invalidated after this function.
  AuctionResult input;
  input.CopyFrom(auction_result);
  auto output = MapAuctionResultToAdWithBidMetadata(input);

  EXPECT_EQ(auction_result.bid(), output->bid());
  EXPECT_EQ(auction_result.ad_render_url(), output->render());
  EXPECT_EQ(auction_result.interest_group_name(),
            output->interest_group_name());
  EXPECT_EQ(auction_result.interest_group_owner(),
            output->interest_group_owner());
  EXPECT_EQ(auction_result.bid_currency(), output->bid_currency());
  EXPECT_EQ(output->ad_components_size(),
            auction_result.ad_component_render_urls_size());
  for (auto& ad_component_url : auction_result.ad_component_render_urls()) {
    EXPECT_THAT(output->ad_components(), testing::Contains(ad_component_url));
  }
}

constexpr char kTestAdMetadataJson[] = R"JSON({"arbitraryMetadataKey":2})JSON";
constexpr char kTestScoringSignals[] = R"JSON({"RandomScoringSignals":{}})JSON";
constexpr char kTestAuctionConfig[] = R"JSON({"RandomAuctionConfig":{}})JSON";
constexpr char kTestBidMetadata[] = R"JSON({"RandomBidMetadata":{}})JSON";
server_common::log::ContextImpl log_context{
    {}, server_common::ConsentedDebugConfiguration()};

TEST(BuildScoreAdRequestTest, PopulatesExpectedValuesInDispatchRequest) {
  float test_bid = MakeARandomNumber<float>(0.1, 10.1);
  auto output = BuildScoreAdRequest(
      kTestRenderUrl, kTestAdMetadataJson, kTestScoringSignals, test_bid,
      std::make_shared<std::string>(kTestAuctionConfig), kTestBidMetadata,
      log_context,
      /*enable_adtech_code_logging = */ false,
      /*enable_debug_reporting = */ false);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->id, kTestRenderUrl);
  EXPECT_EQ(output->version_string, "v1");
  EXPECT_EQ(output->handler_name, DispatchHandlerFunctionWithSellerWrapper);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)],
            kTestAdMetadataJson);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kBid)],
            std::to_string(test_bid));
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)],
            kTestAuctionConfig);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)],
            kTestScoringSignals);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kBidMetadata)],
            kTestBidMetadata);
  EXPECT_EQ(
      *output->input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)],
      "{}");
}

AdWithBidMetadata MakeAnAdWithMetadata(float bid) {
  AdWithBidMetadata ad;
  ad.set_render(kTestRenderUrl);
  ad.set_bid(bid);
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(kTestRenderUrl, "arbitraryMetadataKey", 2));
  return ad;
}

absl::flat_hash_map<std::string, rapidjson::StringBuffer>
MakeScoringSignalsForAd() {
  absl::flat_hash_map<std::string, rapidjson::StringBuffer>
      combined_formatted_ad_signals;
  rapidjson::Document scoring_signal;
  scoring_signal.Parse(kTestScoringSignals);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  scoring_signal.Accept(writer);
  combined_formatted_ad_signals.try_emplace(kTestRenderUrl, std::move(buffer));
  return combined_formatted_ad_signals;
}

TEST(BuildScoreAdRequestTest, PopulatesExpectedValuesForAdWithBidMetadata) {
  float test_bid = MakeARandomNumber<float>(0.1, 10.1);
  auto output = BuildScoreAdRequest(
      MakeAnAdWithMetadata(test_bid),
      std::make_shared<std::string>(kTestAuctionConfig),
      MakeScoringSignalsForAd(), /*enable_debug_reporting = */ false,
      log_context, /*enable_adtech_code_logging = */ false, kTestBidMetadata);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->id, kTestRenderUrl);
  EXPECT_EQ(output->version_string, "v1");
  EXPECT_EQ(output->handler_name, DispatchHandlerFunctionWithSellerWrapper);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)],
            kTestAdMetadataJson);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kBid)],
            std::to_string(test_bid));
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)],
            kTestAuctionConfig);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)],
            kTestScoringSignals);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kBidMetadata)],
            kTestBidMetadata);
  EXPECT_EQ(
      *output->input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)],
      "{}");
}

TEST(BuildScoreAdRequestTest,
     PopulatesExpectedValuesForForProtectedAppSignalsAd) {
  float test_bid = MakeARandomNumber<float>(0.1, 10.1);
  auto output = BuildScoreAdRequest(
      GetProtectedAppSignalsAdWithBidMetadata(kTestRenderUrl, test_bid),
      std::make_shared<std::string>(kTestAuctionConfig),
      MakeScoringSignalsForAd(), /*enable_debug_reporting = */ false,
      log_context, /*enable_adtech_code_logging = */ false, kTestBidMetadata);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->id, kTestRenderUrl);
  EXPECT_EQ(output->version_string, "v1");
  EXPECT_EQ(output->handler_name, DispatchHandlerFunctionWithSellerWrapper);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)],
            kTestAdMetadataJson);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kBid)],
            std::to_string(test_bid));
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kAuctionConfig)],
            kTestAuctionConfig);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kScoringSignals)],
            kTestScoringSignals);
  EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kBidMetadata)],
            kTestBidMetadata);
  EXPECT_EQ(
      *output->input[ScoreArgIndex(ScoreAdArgs::kDirectFromSellerSignals)],
      "{}");
}

std::string MakeFeatureFlagJson(bool enable_adtech_code_logging,
                                bool enable_debug_reporting) {
  return absl::StrCat("{\"", "enable_logging\": ",
                      enable_adtech_code_logging ? "true" : "false",
                      ",\"enable_debug_url_generation\": ",
                      enable_debug_reporting ? "true" : "false", "}");
}

TEST(BuildScoreAdRequestTest, PopulatesFeatureFlagsInDispatchRequest) {
  std::vector<bool> flag_values = {true, false};
  for (auto flag_1 : flag_values) {
    for (auto flag_2 : flag_values) {
      auto output = BuildScoreAdRequest(
          kTestRenderUrl, kTestAdMetadataJson, kTestScoringSignals,
          MakeARandomNumber<float>(0.1, 10.1),
          std::make_shared<std::string>(kTestAuctionConfig), kTestBidMetadata,
          log_context,
          /*enable_adtech_code_logging = */ flag_1,
          /*enable_debug_reporting = */ flag_2);
      ASSERT_TRUE(output.ok());
      EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)],
                MakeFeatureFlagJson(flag_1, flag_2));
    }
  }
}

TEST(BuildScoreAdRequestTest, PopulatesFeatureFlagsForAdWithBidMetadata) {
  std::vector<bool> flag_values = {true, false};
  for (auto flag_1 : flag_values) {
    for (auto flag_2 : flag_values) {
      auto output = BuildScoreAdRequest(
          MakeAnAdWithMetadata(MakeARandomNumber<float>(0.1, 10.1)),
          std::make_shared<std::string>(kTestAuctionConfig),
          MakeScoringSignalsForAd(), /*enable_debug_reporting = */ flag_2,
          log_context, /*enable_adtech_code_logging = */ flag_1,
          kTestBidMetadata);
      ASSERT_TRUE(output.ok());
      EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)],
                MakeFeatureFlagJson(flag_1, flag_2));
    }
  }
}

TEST(BuildScoreAdRequestTest, PopulatesFeatureFlagsForPASAdWithBidMetadata) {
  std::vector<bool> flag_values = {true, false};
  for (auto flag_1 : flag_values) {
    for (auto flag_2 : flag_values) {
      auto output = BuildScoreAdRequest(
          GetProtectedAppSignalsAdWithBidMetadata(kTestRenderUrl),
          std::make_shared<std::string>(kTestAuctionConfig),
          MakeScoringSignalsForAd(), /*enable_debug_reporting = */ flag_2,
          log_context, /*enable_adtech_code_logging = */ flag_1,
          kTestBidMetadata);
      ASSERT_TRUE(output.ok());
      EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)],
                MakeFeatureFlagJson(flag_1, flag_2));
    }
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
