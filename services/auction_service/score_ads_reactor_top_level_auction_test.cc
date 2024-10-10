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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/reporting/reporting_helper_test_constants.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestSellerSignals[] = R"json({"seller_signal": "test 1"})json";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test 2"})json";
constexpr char kTestPublisherHostname[] = "publisher_hostname";
constexpr char kTestTopLevelSeller[] = "top_level_seller";
constexpr char kTestGenerationId[] = "test_generation_id";
constexpr char kTestComponentWinReportingUrl[] =
    "http://componentReportingUrl.com";
constexpr char kTestComponentEvent[] = "click";
constexpr char kTestComponentInteractionReportingUrl[] =
    "http://componentInteraction.com";
constexpr char kTestComponentSeller[] = "http://componentSeller.com";

using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;

RawRequest BuildTopLevelAuctionRawRequest(
    const std::vector<AuctionResult>& component_auctions,
    const std::string& seller_signals, const std::string& auction_signals,
    const std::string& publisher_hostname) {
  RawRequest output;
  for (int i = 0; i < component_auctions.size(); i++) {
    // Copy to preserve original for test verification.
    *output.mutable_component_auction_results()->Add() = component_auctions[i];
  }
  output.set_seller_signals(seller_signals);
  output.set_auction_signals(auction_signals);
  output.clear_scoring_signals();
  output.set_publisher_hostname(publisher_hostname);
  output.set_enable_debug_reporting(false);
  return output;
}

class ScoreAdsReactorTopLevelAuctionTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(ScoreAdsReactorTopLevelAuctionTest, SendsComponentAuctionsToDispatcher) {
  MockV8DispatchClient dispatcher;
  AuctionResult car_1 =
      MakeARandomComponentAuctionResult(kTestGenerationId, kTestTopLevelSeller);
  AuctionResult car_2 =
      MakeARandomComponentAuctionResult(kTestGenerationId, kTestTopLevelSeller);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1, car_2}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);
  EXPECT_EQ(raw_request.component_auction_results_size(), 2);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([&car_1, &car_2](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 2);
        // Actual mapping of other fields from component auction
        // to dispatch request handled by proto_utils.
        EXPECT_EQ(batch.at(0).id, car_1.ad_render_url());
        EXPECT_EQ(batch.at(0).input.size(), 7);
        EXPECT_EQ(batch.at(1).id, car_2.ad_render_url());
        EXPECT_EQ(batch.at(1).input.size(), 7);
        return absl::OkStatus();
      });
  ScoreAdsReactorTestHelper test_helper;
  test_helper.ExecuteScoreAds(raw_request, dispatcher);
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest,
       DoesNotRunAuctionForMismatchedGenerationId) {
  MockV8DispatchClient dispatcher;
  AuctionResult car_1 = MakeARandomComponentAuctionResult(MakeARandomString(),
                                                          kTestTopLevelSeller);
  AuctionResult car_2 = MakeARandomComponentAuctionResult(MakeARandomString(),
                                                          kTestTopLevelSeller);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1, car_2}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);
  EXPECT_EQ(raw_request.component_auction_results_size(), 2);
  EXPECT_CALL(dispatcher, BatchExecute).Times(0);
  ScoreAdsReactorTestHelper test_helper;
  auto response = test_helper.ExecuteScoreAds(raw_request, dispatcher);
  EXPECT_TRUE(response.response_ciphertext().empty());
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest,
       ReturnsWinnerWithComponentUrlsWhenReportingDisabled) {
  MockV8DispatchClient dispatcher;
  TestComponentAuctionResultData component_auction_result_data = {
      .test_component_seller = kTestComponentSeller,
      .generation_id = kTestGenerationId,
      .test_ig_owner = MakeARandomString(),
      .test_component_win_reporting_url = kTestComponentWinReportingUrl,
      .test_component_report_result_url = kTestComponentWinReportingUrl,
      .test_component_event = kTestComponentEvent,
      .test_component_interaction_reporting_url =
          kTestComponentInteractionReportingUrl};

  AuctionResult car_1 = MakeARandomComponentAuctionResultWithReportingUrls(
      component_auction_result_data);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1}, kTestSellerSignals, kTestAuctionSignals, kTestPublisherHostname);
  bool enable_report_win_url_generation = false;
  bool enable_report_result_url_generation = false;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportResultEntryFunction) == 0) {
            response.emplace_back(kTestReportResultResponseJson);
          } else {
            response.push_back(
                R"JSON(
                {
                    "response" : {
                        "ad": {"key1":"adMetadata"},
                        "desirability" : 1,
                        "bid" : 0.1,
                        "allowComponentAuction" : true
                    },
                    "logs":[]
                }
              )JSON");
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           false);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = false,
      .enable_adtech_code_logging = false,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_win_url_generation,
      .enable_seller_and_buyer_udf_isolation = true};
  ScoreAdsReactorTestHelper test_helper;
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), 1);
  EXPECT_EQ(scored_ad.render(), car_1.ad_render_url());
  EXPECT_EQ(scored_ad.component_renders_size(),
            car_1.ad_component_render_urls_size());
  EXPECT_EQ(scored_ad.interest_group_name(), car_1.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(), car_1.interest_group_owner());
  EXPECT_EQ(scored_ad.buyer_bid(), car_1.bid());
  EXPECT_TRUE(scored_ad.win_reporting_urls()
                  .top_level_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentWinReportingUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestComponentInteractionReportingUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestComponentWinReportingUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestComponentInteractionReportingUrl);
  // Since in the above test we are assuming non-component auctions, check that
  // the required fields for component auctions are not set.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_TRUE(scored_ad.ad_metadata().empty());
  EXPECT_EQ(scored_ad.bid(), 0);
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest,
       ReturnsWinningAdFromDispatcherWithReportingEnabled) {
  MockV8DispatchClient dispatcher;
  TestComponentAuctionResultData component_auction_result_data = {
      .test_component_seller = kTestComponentSeller,
      .generation_id = kTestGenerationId,
      .test_ig_owner = MakeARandomString(),
      .test_component_win_reporting_url = kTestComponentWinReportingUrl,
      .test_component_report_result_url = kTestComponentWinReportingUrl,
      .test_component_event = kTestComponentEvent,
      .test_component_interaction_reporting_url =
          kTestComponentInteractionReportingUrl};
  AuctionResult car_1 = MakeARandomComponentAuctionResultWithReportingUrls(
      component_auction_result_data);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1}, kTestSellerSignals, kTestAuctionSignals, kTestPublisherHostname);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportResultEntryFunction) == 0) {
            response.emplace_back(kTestReportResultResponseJson);
          } else {
            response.push_back(
                R"JSON(
                {
                    "response" : {
                        "ad": {"key1":"adMetadata"},
                        "desirability" : 1,
                        "bid" : 0.1,
                        "allowComponentAuction" : true
                    },
                    "logs":[]
                }
              )JSON");
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           false);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = false,
      .enable_adtech_code_logging = false,
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  ScoreAdsReactorTestHelper test_helper;
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestTopLevelReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestInteractionUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentWinReportingUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestComponentInteractionReportingUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestComponentWinReportingUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestComponentInteractionReportingUrl);
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest,
       NoUrlsReturnedWhenNoComponentUrlsPresent) {
  MockV8DispatchClient dispatcher;
  AuctionResult car_1 = MakeARandomComponentAuctionResult(MakeARandomString(),
                                                          kTestTopLevelSeller);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1}, kTestSellerSignals, kTestAuctionSignals, kTestPublisherHostname);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportResultEntryFunction) == 0) {
            response.emplace_back(kTestReportResultResponseJson);
          } else {
            response.push_back(
                R"JSON(
                {
                    "response" : {
                        "ad": {"key1":"adMetadata"},
                        "desirability" : 1,
                        "bid" : 0.1,
                        "allowComponentAuction" : true
                    },
                    "logs":[]
                }
              )JSON");
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           false);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = false,
      .enable_adtech_code_logging = false,
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  ScoreAdsReactorTestHelper test_helper;
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestTopLevelReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestInteractionUrl);
  EXPECT_TRUE(scored_ad.win_reporting_urls()
                  .component_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
  EXPECT_TRUE(scored_ad.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest, DoesNotPopulateHighestOtherBid) {
  MockV8DispatchClient dispatcher;
  AuctionResult car_1 =
      MakeARandomComponentAuctionResult(kTestGenerationId, kTestTopLevelSeller);
  AuctionResult car_2 =
      MakeARandomComponentAuctionResult(kTestGenerationId, kTestTopLevelSeller);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1, car_2}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);
  EXPECT_EQ(raw_request.component_auction_results_size(), 2);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportResultEntryFunction) == 0) {
            response.emplace_back(kTestReportResultResponseJson);
          } else {
            response.push_back(
                R"JSON(
                {
                    "response" : {
                        "ad": {"key1":"adMetadata"},
                        "desirability" : 1,
                        "bid" : 0.1,
                        "allowComponentAuction" : true
                    },
                    "logs":[]
                }
              )JSON");
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           false);
      });
  ScoreAdsReactorTestHelper test_helper;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = false,
      .enable_adtech_code_logging = false,
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  // Do not populate highest scoring other bids.
  EXPECT_TRUE(scored_ad.ig_owner_highest_scoring_other_bids_map().empty());
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest, DoesNotPerformDebugReporting) {
  MockV8DispatchClient dispatcher;
  AuctionResult car_1 = MakeARandomComponentAuctionResult(MakeARandomString(),
                                                          kTestTopLevelSeller);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1}, kTestSellerSignals, kTestAuctionSignals, kTestPublisherHostname);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportResultEntryFunction) == 0) {
            response.emplace_back(kTestReportResultResponseJson);
          } else {
            response.push_back(
                R"JSON(
                {
                    "response" : {
                        "ad": {"key1":"adMetadata"},
                        "desirability" : 1,
                        "bid" : 0.1,
                        "allowComponentAuction" : true
                    },
                    "logs":[]
                }
              )JSON");
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           false);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = false,
      .enable_adtech_code_logging = false,
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  ScoreAdsReactorTestHelper test_helper;
  EXPECT_CALL(*test_helper.async_reporter, DoReport).Times(0);
  test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
