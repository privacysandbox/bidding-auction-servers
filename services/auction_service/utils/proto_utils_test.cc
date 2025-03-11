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
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/common/test/random.h"
#include "services/common/util/json_util.h"
#include "services/common/util/proto_util.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestPublisher[] = "publisher.com";
constexpr char kTestIGOwner[] = "interest_group_owner";
constexpr char kTestIGName[] = "interest_group_name";
constexpr SellerRejectionReason kTestSellerRejectionReason =
    SellerRejectionReason::INVALID_BID;
constexpr char kTestRenderUrl[] = "https://render_url";
constexpr char kTestComponentSeller[] = "component_seller_origin";
constexpr char kTestTopLevelSeller[] = "top_level_seller_origin";
constexpr char kTestAdComponentUrl_1[] = "https://compoennt_1";
constexpr char kTestAdComponentUrl_2[] = "https://component_2";
constexpr char kTestBidCurrency[] = "ABC";
constexpr uint32_t kTestSellerDataVersion = 1989;
constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
constexpr char kTestBuyerAndSellerReportingId[] =
    "testBuyerAndSellerReportingId";
constexpr char kTestSelectedReportingId[] =
    "testSelectedBuyerAndSellerReportingId";

using ::google::protobuf::util::MessageDifferencer;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using google::scp::core::test::EqualsProto;

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

void CheckGenericOutput(const rapidjson::Document& parsed_output) {
  EXPECT_EQ(parsed_output["topWindowHostname"], kTestPublisher);
  EXPECT_EQ(parsed_output["interestGroupOwner"], kTestIGOwner);
  EXPECT_EQ(parsed_output["renderUrl"], kTestRenderUrl);
  ASSERT_TRUE(parsed_output["adComponents"].IsArray());
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[0], kTestAdComponentUrl_1);
  EXPECT_EQ(parsed_output["adComponents"].GetArray()[1], kTestAdComponentUrl_2);
  EXPECT_EQ(parsed_output["bidCurrency"], kTestBidCurrency);
  ASSERT_TRUE(parsed_output["dataVersion"].IsUint());
  EXPECT_EQ(parsed_output["dataVersion"].GetUint(), kTestSellerDataVersion);
}

TEST(MakeBidMetadataForTopLevelAuctionTest, PopulatesExpectedValues) {
  std::string output = MakeBidMetadataForTopLevelAuction(
      kTestPublisher, kTestIGOwner, kTestRenderUrl, MakeMockAdComponentUrls(),
      kTestComponentSeller, kTestBidCurrency, kTestSellerDataVersion);

  const rapidjson::Document parsed_output = ParseJson(output);
  CheckGenericOutput(parsed_output);
  EXPECT_EQ(parsed_output["componentSeller"], kTestComponentSeller);
}

TEST(MakeBidMetadataTest, PopulatesExpectedValues) {
  std::string output = MakeBidMetadata(
      kTestPublisher, kTestIGOwner, kTestRenderUrl, MakeMockAdComponentUrls(),
      "", kTestBidCurrency, kTestSellerDataVersion);

  const rapidjson::Document parsed_output = ParseJson(output);
  CheckGenericOutput(parsed_output);
}

TEST(MakeBidMetadataTest, PopulatesExpectedValuesForComponentAuction) {
  std::string output = MakeBidMetadata(
      kTestPublisher, kTestIGOwner, kTestRenderUrl, MakeMockAdComponentUrls(),
      kTestTopLevelSeller, kTestBidCurrency, kTestSellerDataVersion);

  const rapidjson::Document parsed_output = ParseJson(output);
  CheckGenericOutput(parsed_output);
  EXPECT_EQ(parsed_output["topLevelSeller"], kTestTopLevelSeller);
}

TEST(MakeBidMetadataTest, PopulatesExpectedReportingIds) {
  ReportingIdsParamForBidMetadata reporting_ids = {
      .buyer_reporting_id = kTestBuyerReportingId,
      .buyer_and_seller_reporting_id = kTestBuyerAndSellerReportingId,
      .selected_buyer_and_seller_reporting_id = kTestSelectedReportingId};
  std::string output =
      MakeBidMetadata(kTestPublisher, kTestIGOwner, kTestRenderUrl,
                      MakeMockAdComponentUrls(), kTestTopLevelSeller,
                      kTestBidCurrency, kTestSellerDataVersion, reporting_ids);

  const rapidjson::Document parsed_output = ParseJson(output);
  CheckGenericOutput(parsed_output);
  EXPECT_EQ(parsed_output[kBuyerReportingIdForScoreAd], kTestBuyerReportingId);
  EXPECT_EQ(parsed_output[kBuyerAndSellerReportingIdForScoreAd],
            kTestBuyerAndSellerReportingId);
  EXPECT_EQ(parsed_output[kSelectedBuyerAndSellerReportingIdForScoreAd],
            kTestSelectedReportingId);
}

TEST(MapAuctionResultToAdWithBidMetadataTest, PopulatesExpectedValues) {
  AuctionResult auction_result = MakeARandomComponentAuctionResult(
      MakeARandomString(), kTestTopLevelSeller);

  // copy since this will be invalidated after this function.
  AuctionResult input;
  input.CopyFrom(auction_result);
  auto output =
      MapAuctionResultToAdWithBidMetadata(input, /*k_anon_status=*/true);

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
  EXPECT_TRUE(output->k_anon_status());
}

TEST(MapAuctionResultToProtectedAppSignalsAdWithBidMetadataTest,
     PopulatesExpectedValues) {
  AuctionResult auction_result = MakeARandomPASComponentAuctionResult(
      MakeARandomString(), kTestTopLevelSeller);

  // copy since this will be invalidated after this function.
  AuctionResult input;
  input.CopyFrom(auction_result);
  auto output = MapAuctionResultToProtectedAppSignalsAdWithBidMetadata(
      input, /*k_anon_status=*/true);

  EXPECT_EQ(auction_result.bid(), output->bid());
  EXPECT_EQ(auction_result.ad_render_url(), output->render());
  EXPECT_EQ(auction_result.interest_group_owner(), output->owner());
  EXPECT_EQ(auction_result.bid_currency(), output->bid_currency());
  EXPECT_TRUE(output->k_anon_status());
}

constexpr absl::string_view kTestAdMetadataJson = R"JSON(
  {
    "metadata": {
      "arbitraryMetadataKey": 2
    },
    "renderUrl": "https://render_url"
  })JSON";
constexpr char kTestScoringSignals[] = R"JSON({"RandomScoringSignals":{}})JSON";
constexpr char kTestAuctionConfig[] = R"JSON({"RandomAuctionConfig":{}})JSON";
constexpr char kTestBidMetadata[] = R"JSON({"RandomBidMetadata":{}})JSON";
RequestLogContext log_context{{}, server_common::ConsentedDebugConfiguration()};

google::protobuf::Value GetTestAdMetadata() {
  auto ads_metadata = JsonStringToValue(kTestAdMetadataJson);
  CHECK_OK(ads_metadata) << "Malformed JSON: " << kTestAdMetadataJson;
  return *ads_metadata;
}

TEST(BuildScoreAdRequestTest, PopulatesExpectedValuesInDispatchRequest) {
  float test_bid = MakeARandomNumber<float>(0.1, 10.1);
  auto output = BuildScoreAdRequest(
      kTestRenderUrl, kTestAdMetadataJson, kTestScoringSignals, test_bid,
      std::make_shared<std::string>(kTestAuctionConfig), kTestBidMetadata,
      log_context,
      /*enable_adtech_code_logging = */ false,
      /*enable_debug_reporting = */ false, kScoreAdBlobVersion);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->id, kTestRenderUrl);
  EXPECT_EQ(output->version_string, kScoreAdBlobVersion);
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

TEST(BuildScoreAdRequestTest, HandlesAdsMetadataString) {
  float test_bid = MakeARandomNumber<float>(0.1, 10.1);
  std::string ad_metadata_json = R"JSON("test_ads_data")JSON";
  auto output = BuildScoreAdRequest(
      kTestRenderUrl, ad_metadata_json, kTestScoringSignals, test_bid,
      std::make_shared<std::string>(kTestAuctionConfig), kTestBidMetadata,
      log_context,
      /*enable_adtech_code_logging = */ false,
      /*enable_debug_reporting = */ false, kScoreAdBlobVersion);
  auto observed_ad = JsonStringToValue(
      *output->input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)]);
  CHECK_OK(observed_ad)
      << "Malformed observed ad JSON: "
      << *output->input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
  std::string difference;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  auto expected_ad = JsonStringToValue(ad_metadata_json);
  CHECK_OK(expected_ad) << "Malformed JSON: " << ad_metadata_json;
  EXPECT_TRUE(differencer.Compare(*observed_ad, *expected_ad))
      << "\n Observed:\n"
      << observed_ad->DebugString() << "\n\nExpected:\n"
      << expected_ad->DebugString() << "\n\nDifference:\n"
      << difference;
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
  auto scoring_signals = MakeScoringSignalsForAd();
  auto output = BuildScoreAdRequest(
      MakeAnAdWithMetadata(test_bid),
      std::make_shared<std::string>(kTestAuctionConfig),
      scoring_signals.find(kTestRenderUrl)->second.GetString(),
      /*enable_debug_reporting = */ false, log_context,
      /*enable_adtech_code_logging = */ false, kTestBidMetadata,
      kScoreAdBlobVersion);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->id, kTestRenderUrl);
  EXPECT_EQ(output->version_string, kScoreAdBlobVersion);
  EXPECT_EQ(output->handler_name, DispatchHandlerFunctionWithSellerWrapper);
  auto observed_ad = JsonStringToValue(
      *output->input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)]);
  CHECK_OK(observed_ad)
      << "Malformed observed ad JSON: "
      << *output->input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
  std::string difference;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  auto expected_ad = GetTestAdMetadata();
  EXPECT_TRUE(differencer.Compare(*observed_ad, expected_ad))
      << "\n Observed:\n"
      << observed_ad->DebugString() << "\n\nExpected:\n"
      << expected_ad.DebugString() << "\n\nDifference:\n"
      << difference;
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
  auto scoring_signals = MakeScoringSignalsForAd();
  auto output = BuildScoreAdRequest(
      GetProtectedAppSignalsAdWithBidMetadata(kTestRenderUrl, test_bid),
      std::make_shared<std::string>(kTestAuctionConfig),
      scoring_signals.find(kTestRenderUrl)->second.GetString(),
      /*enable_debug_reporting = */ false, log_context,
      /*enable_adtech_code_logging = */ false, kTestBidMetadata,
      kScoreAdBlobVersion);
  ASSERT_TRUE(output.ok());
  EXPECT_EQ(output->id, kTestRenderUrl);
  EXPECT_EQ(output->version_string, "v1");
  EXPECT_EQ(output->handler_name, DispatchHandlerFunctionWithSellerWrapper);
  auto observed_ad = JsonStringToValue(
      *output->input[ScoreArgIndex(ScoreAdArgs::kAdMetadata)]);
  CHECK_OK(observed_ad)
      << "Malformed observed ad JSON: "
      << *output->input[static_cast<int>(ScoreAdArgs::kAdMetadata)];
  std::string difference;
  google::protobuf::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  auto expected_ad = GetTestAdMetadata();
  EXPECT_TRUE(differencer.Compare(*observed_ad, expected_ad))
      << "\n Observed:\n"
      << observed_ad->DebugString() << "\n\nExpected:\n"
      << expected_ad.DebugString() << "\n\nDifference:\n"
      << difference;
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
          /*enable_debug_reporting = */ flag_2, kScoreAdBlobVersion);
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
      auto scoring_signals = MakeScoringSignalsForAd();
      auto output = BuildScoreAdRequest(
          MakeAnAdWithMetadata(MakeARandomNumber<float>(0.1, 10.1)),
          std::make_shared<std::string>(kTestAuctionConfig),
          scoring_signals.find(kTestRenderUrl)->second.GetString(),
          /*enable_debug_reporting = */ flag_2, log_context,
          /*enable_adtech_code_logging = */ flag_1, kTestBidMetadata,
          kScoreAdBlobVersion);
      ASSERT_TRUE(output.ok());
      EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)],
                MakeFeatureFlagJson(flag_1, flag_2));
    }
  }
}

TEST(BuildAdRejectionReasonTest, BuildsAdRejectionReason) {
  ScoreAdsResponse::AdScore::AdRejectionReason ad_rejection_reason =
      BuildAdRejectionReason(kTestIGOwner, kTestIGName,
                             kTestSellerRejectionReason);
  EXPECT_EQ(ad_rejection_reason.interest_group_owner(), kTestIGOwner);
  EXPECT_EQ(ad_rejection_reason.interest_group_name(), kTestIGName);
  EXPECT_EQ(ad_rejection_reason.rejection_reason(), kTestSellerRejectionReason);
}

TEST(BuildScoreAdRequestTest, PopulatesFeatureFlagsForPASAdWithBidMetadata) {
  std::vector<bool> flag_values = {true, false};
  for (auto flag_1 : flag_values) {
    for (auto flag_2 : flag_values) {
      auto scoring_signals = MakeScoringSignalsForAd();
      auto output = BuildScoreAdRequest(
          GetProtectedAppSignalsAdWithBidMetadata(kTestRenderUrl),
          std::make_shared<std::string>(kTestAuctionConfig),
          scoring_signals.find(kTestRenderUrl)->second.GetString(),
          /*enable_debug_reporting = */ flag_2, log_context,
          /*enable_adtech_code_logging = */ flag_1, kTestBidMetadata,
          kScoreAdBlobVersion);
      ASSERT_TRUE(output.ok());
      EXPECT_EQ(*output->input[ScoreArgIndex(ScoreAdArgs::kFeatureFlags)],
                MakeFeatureFlagJson(flag_1, flag_2));
    }
  }
}

TEST(ScoreAdsTest, ParsesScoreAdResponseRespectsDebugUrlLimits) {
  std::string long_win_url(1024, 'A');
  std::string long_loss_url(1025, 'B');
  auto scored_ad = ParseJsonString(absl::Substitute(
      R"({
                  "desirability" : 10,
                  "allowComponentAuction" : false,
                  "debugReportUrls": {
                    "auctionDebugLossUrl": "$0",
                    "auctionDebugWinUrl": "$1"
                  }
                })",
      long_loss_url, long_win_url));
  CHECK_OK(scored_ad);
  int64_t current_all_debug_urls_chars = 0;
  auto parsed_response = ScoreAdResponseJsonToProto(
      *scored_ad, /*max_allowed_size_debug_url_chars=*/65536,
      /*max_allowed_size_all_debug_urls_chars=*/1024,
      /*device_component_auction=*/false, current_all_debug_urls_chars);
  CHECK_OK(parsed_response);
  std::cerr << " parsed response: \n" << parsed_response->DebugString() << "\n";
  EXPECT_TRUE(parsed_response->has_debug_report_urls());
  EXPECT_FALSE(
      parsed_response->debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(parsed_response->debug_report_urls().auction_debug_win_url().size(),
            1024);
  EXPECT_TRUE(
      parsed_response->debug_report_urls().auction_debug_loss_url().empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
