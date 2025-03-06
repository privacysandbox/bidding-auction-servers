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

#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "src/core/test/utils/proto_test_utils.h"

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
constexpr char kTestComponentEvent[] = "clickEvent";
constexpr char kTestComponentInteractionReportingUrl[] =
    "http://componentInteraction.com";
constexpr char kTestComponentSeller[] = "http://componentSeller.com";
constexpr char kTestIgName[] = "TestIg";
constexpr char kTestIgOwner[] = "TestIgOwner";
constexpr char kTestAdRenderUrl[] = "http://test-ad-render-url.com";
constexpr char kTestGhostWinnerAdRenderUrl[] =
    "http://ghost-winner-ad-render-url.com";
constexpr float kTestScore = 1.92;
constexpr char kTestAdComponentRenderUrl[] =
    "http://test-ad-component-render-url";
constexpr int kTestIgIndex = 1;
constexpr float kTestGhostWinnerBid = 2.0;
constexpr float kTestWinnerBid = 1.0;
constexpr char kTestBidCurrency[] = "testCurrency";
constexpr char kTestAdMetadata[] = "testAdMetadata";
constexpr char kTestSelectedBuyerAndSellerReportingId[] =
    "testSelectedBuyerAndSellerReportingId";
constexpr char kTestAdComponentAdRenderUrl[] =
    "http://ad-component-render-url.com";
constexpr char kTestGhostWinnerIgOwner[] = "TestGhostWinnerIgOwner";
constexpr char kTestGhostWinnerIgOrigin[] = "TestGhostWinnerIgOrigin";
constexpr char kTestGhostWinnerIgName[] = "TestGhostWinnerIgName";
constexpr char kTestAdRenderUrlHash[] = "TestAdRenderUrlHash";
constexpr char kTestAdComponentRenderUrlsHash[] =
    "TestAdComponentRenderUrlsHash";
constexpr char kTestReportingIdHash[] = "TestReportingIdHash";
constexpr char kTestWinnerAdRenderUrlHash[] = "TestWinnerAdRenderUrlHash";
constexpr char kTestWinnerAdComponentRenderUrlsHash[] =
    "TestWinnerAdComponentRenderUrlsHash";
constexpr char kTestWinnerReportingIdHash[] = "TestWinnerReportingIdHash";

using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using google::scp::core::test::EqualsProto;

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

TEST_F(ScoreAdsReactorTopLevelAuctionTest, SendsPASAuctionsToDispatcher) {
  MockV8DispatchClient dispatcher;
  AuctionResult car_1 =
      MakeARandomComponentAuctionResult(kTestGenerationId, kTestTopLevelSeller);
  AuctionResult car_2 = MakeARandomPASComponentAuctionResult(
      kTestGenerationId, kTestTopLevelSeller);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {car_1, car_2}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);
  absl::Notification finished;
  EXPECT_EQ(raw_request.component_auction_results_size(), 2);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce(
          [&car_1, &car_2, &finished](std::vector<DispatchRequest>& batch,
                                      BatchDispatchDoneCallback done_callback) {
            EXPECT_EQ(batch.size(), 2);
            // Actual mapping of other fields from component auction
            // to dispatch request handled by proto_utils.
            EXPECT_EQ(batch.at(0).id, car_1.ad_render_url());
            EXPECT_EQ(batch.at(0).input.size(), 7);
            EXPECT_EQ(batch.at(1).id, car_2.ad_render_url());
            EXPECT_EQ(batch.at(1).input.size(), 7);
            finished.Notify();
            return absl::OkStatus();
          });
  ScoreAdsReactorTestHelper test_helper;
  test_helper.ExecuteScoreAds(raw_request, dispatcher);
  finished.WaitForNotification();
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

TEST_F(ScoreAdsReactorTopLevelAuctionTest, ReturnsPASWinnerWithReportingUrls) {
  auto win_reporting_urls =
      WinReportingUrlsBuilder()
          .SetBuyerReportingUrls(
              WinReportingUrls_ReportingUrlsBuilder()
                  .SetReportingUrl(kTestComponentWinReportingUrl)
                  .InsertInteractionReportingUrls(
                      {kTestComponentEvent,
                       kTestComponentInteractionReportingUrl}))

          .SetComponentSellerReportingUrls(
              WinReportingUrls_ReportingUrlsBuilder()
                  .SetReportingUrl(kTestComponentWinReportingUrl)
                  .InsertInteractionReportingUrls(
                      {kTestComponentEvent,
                       kTestComponentInteractionReportingUrl}));
  AuctionResult component_auction_result =
      AuctionResultBuilder()
          .SetTopLevelSeller(kTestTopLevelSeller)
          .SetAdRenderUrl(kTestAdRenderUrl)
          .SetInterestGroupOwner(kTestIgOwner)
          .SetScore(kTestScore)
          .SetAdType(AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD)
          .SetAuctionParams(AuctionResult_AuctionParamsBuilder()
                                .SetCiphertextGenerationId(kTestGenerationId)
                                .SetComponentSeller(kTestComponentSeller))
          .SetWinReportingUrls(win_reporting_urls)
          .SetBid(kTestWinnerBid);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {component_auction_result}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);

  MockV8DispatchClient dispatcher;
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
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_kanon = false};
  ScoreAdsReactorTestHelper test_helper;
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  PS_VLOG(5) << "Score Ad raw response: " << raw_response.DebugString();

  ScoreAdsResponse::ScoreAdsRawResponse expected_raw_response;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_score {
          desirability: 1
          render: "http://test-ad-render-url.com"
          buyer_bid: 1
          interest_group_owner: "TestIgOwner"
          win_reporting_urls {
            buyer_reporting_urls {
              reporting_url: "http://componentReportingUrl.com"
              interaction_reporting_urls {
                key: "clickEvent"
                value: "http://componentInteraction.com"
              }
            }
            component_seller_reporting_urls {
              reporting_url: "http://componentReportingUrl.com"
              interaction_reporting_urls {
                key: "clickEvent"
                value: "http://componentInteraction.com"
              }
            }
            top_level_seller_reporting_urls {
              reporting_url: "http://reportResultUrl.com"
              interaction_reporting_urls {
                key: "clickEvent"
                value: "http://event.com"
              }
            }
          }
          ad_type: AD_TYPE_PROTECTED_APP_SIGNALS_AD
          buyer_reporting_id: ""
          buyer_and_seller_reporting_id: ""
          k_anon_join_candidate {}, seller: "http://componentSeller.com"
        }
      )pb",
      &expected_raw_response));
  EXPECT_THAT(raw_response, EqualsProto(expected_raw_response));
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
            kTestTopLevelReportResultUrlInResponse);
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
            kTestTopLevelReportResultUrlInResponse);
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

TEST_F(ScoreAdsReactorTopLevelAuctionTest, ScoresKAnonWinnerAndGhostWinners) {
  auto win_reporting_urls =
      WinReportingUrlsBuilder()
          .SetBuyerReportingUrls(
              WinReportingUrls_ReportingUrlsBuilder()
                  .SetReportingUrl(kTestComponentWinReportingUrl)
                  .InsertInteractionReportingUrls(
                      {kTestComponentEvent,
                       kTestComponentInteractionReportingUrl}))

          .SetComponentSellerReportingUrls(
              WinReportingUrls_ReportingUrlsBuilder()
                  .SetReportingUrl(kTestComponentWinReportingUrl)
                  .InsertInteractionReportingUrls(
                      {kTestComponentEvent,
                       kTestComponentInteractionReportingUrl}));
  auto ghost_winner_for_top_level =
      AuctionResult_KAnonGhostWinner_GhostWinnerForTopLevelAuctionBuilder()
          .SetAdRenderUrl(kTestGhostWinnerAdRenderUrl)
          .SetModifiedBid(kTestGhostWinnerBid)
          .SetBidCurrency(kTestBidCurrency)
          .SetAdMetadata(kTestAdMetadata)
          .SetBuyerReportingId(kTestBuyerReportingId)
          .SetSelectedBuyerAndSellerReportingId(
              kTestSelectedBuyerAndSellerReportingId)
          .SetBuyerAndSellerReportingId(kTestBuyerAndSellerReportingId)
          .AddAdComponentRenderUrls(kTestAdComponentAdRenderUrl);
  auto kanon_ghost_winner =
      AuctionResult_KAnonGhostWinnerBuilder()
          .SetInterestGroupIndex(kTestIgIndex)
          .SetOwner(kTestGhostWinnerIgOwner)
          .SetOrigin(kTestGhostWinnerIgOrigin)
          .SetIgName(kTestGhostWinnerIgName)
          .SetGhostWinnerForTopLevelAuction(
              std::move(ghost_winner_for_top_level))
          .SetKAnonJoinCandidates(
              KAnonJoinCandidateBuilder()
                  .SetAdRenderUrlHash(kTestAdRenderUrlHash)
                  .AddAdComponentRenderUrlsHash(kTestAdComponentRenderUrlsHash)
                  .SetReportingIdHash(kTestReportingIdHash));

  AuctionResult component_auction_result =
      AuctionResultBuilder()
          .SetTopLevelSeller(kTestTopLevelSeller)
          .SetAdRenderUrl(kTestAdRenderUrl)
          .SetInterestGroupName(kTestIgName)
          .SetInterestGroupOwner(kTestIgOwner)
          .SetScore(kTestScore)
          .SetAdType(AdType::AD_TYPE_PROTECTED_AUDIENCE_AD)
          .SetAuctionParams(AuctionResult_AuctionParamsBuilder()
                                .SetCiphertextGenerationId(kTestGenerationId)
                                .SetComponentSeller(kTestComponentSeller))
          .SetWinReportingUrls(win_reporting_urls)
          .AddAdComponentRenderUrls(kTestAdComponentRenderUrl)
          .SetBid(kTestWinnerBid)
          .AddKAnonGhostWinners(kanon_ghost_winner)
          .SetKAnonWinnerJoinCandidates(
              KAnonJoinCandidateBuilder()
                  .SetAdRenderUrlHash(kTestWinnerAdRenderUrlHash)
                  .AddAdComponentRenderUrlsHash(
                      kTestWinnerAdComponentRenderUrlsHash)
                  .SetReportingIdHash(kTestWinnerReportingIdHash));
  ASSERT_GT(kTestGhostWinnerBid, kTestWinnerBid)
      << "Ghost winner bid expected to "
      << " be greater than winner bid (since same scores are used for the ads)";
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {component_auction_result}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);
  raw_request.set_enforce_kanon(true);
  raw_request.set_num_allowed_ghost_winners(1);
  MockV8DispatchClient dispatcher;
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
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_kanon = true};
  ScoreAdsReactorTestHelper test_helper;
  PS_VLOG(5) << "Score Ad raw request: " << raw_request.DebugString();
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  PS_VLOG(5) << "Score Ad raw response: " << raw_response.DebugString();

  ScoreAdsResponse::ScoreAdsRawResponse expected_raw_response;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_score {
          desirability: 1
          render: "http://test-ad-render-url.com"
          component_renders: "http://test-ad-component-render-url"
          interest_group_name: "TestIg"
          buyer_bid: 1
          interest_group_owner: "TestIgOwner"
          win_reporting_urls {
            buyer_reporting_urls {
              reporting_url: "http://componentReportingUrl.com"
              interaction_reporting_urls {
                key: "clickEvent"
                value: "http://componentInteraction.com"
              }
            }
            component_seller_reporting_urls {
              reporting_url: "http://componentReportingUrl.com"
              interaction_reporting_urls {
                key: "clickEvent"
                value: "http://componentInteraction.com"
              }
            }
            top_level_seller_reporting_urls {
              reporting_url: "http://reportResultUrl.com"
              interaction_reporting_urls {
                key: "clickEvent"
                value: "http://event.com"
              }
            }
          }
          ad_type: AD_TYPE_PROTECTED_AUDIENCE_AD
          buyer_reporting_id: ""
          buyer_and_seller_reporting_id: ""
          k_anon_join_candidate {
            ad_render_url_hash: "TestWinnerAdRenderUrlHash"
            ad_component_render_urls_hash: "TestWinnerAdComponentRenderUrlsHash"
            reporting_id_hash: "TestWinnerReportingIdHash"
          },
          seller: "http://componentSeller.com"
        }
        ghost_winning_ad_scores {
          desirability: 1
          render: "http://ghost-winner-ad-render-url.com"
          component_renders: "http://ad-component-render-url.com"
          interest_group_name: "TestGhostWinnerIgName"
          buyer_bid: 2
          interest_group_owner: "TestGhostWinnerIgOwner"
          ad_type: AD_TYPE_PROTECTED_AUDIENCE_AD
          buyer_bid_currency: "testCurrency"
          k_anon_join_candidate {
            ad_render_url_hash: "TestAdRenderUrlHash"
            ad_component_render_urls_hash: "TestAdComponentRenderUrlsHash"
            reporting_id_hash: "TestReportingIdHash"
          }
        }
      )pb",
      &expected_raw_response));
  EXPECT_THAT(raw_response, EqualsProto(expected_raw_response));
}

TEST_F(ScoreAdsReactorTopLevelAuctionTest,
       StillScoresGhostWinnerWhenThereIsNoWinner) {
  auto win_reporting_urls =
      WinReportingUrlsBuilder()
          .SetBuyerReportingUrls(
              WinReportingUrls_ReportingUrlsBuilder()
                  .SetReportingUrl(kTestComponentWinReportingUrl)
                  .InsertInteractionReportingUrls(
                      {kTestComponentEvent,
                       kTestComponentInteractionReportingUrl}))
          .SetComponentSellerReportingUrls(
              WinReportingUrls_ReportingUrlsBuilder()
                  .SetReportingUrl(kTestComponentWinReportingUrl)
                  .InsertInteractionReportingUrls(
                      {kTestComponentEvent,
                       kTestComponentInteractionReportingUrl}));
  auto ghost_winner_for_top_level_auction =
      AuctionResult_KAnonGhostWinner_GhostWinnerForTopLevelAuctionBuilder()
          .SetAdRenderUrl(kTestGhostWinnerAdRenderUrl)
          .SetModifiedBid(kTestGhostWinnerBid)
          .SetBidCurrency(kTestBidCurrency)
          .SetAdMetadata(kTestAdMetadata)
          .SetBuyerReportingId(kTestBuyerReportingId)
          .SetSelectedBuyerAndSellerReportingId(
              kTestSelectedBuyerAndSellerReportingId)
          .SetBuyerAndSellerReportingId(kTestBuyerAndSellerReportingId)
          .AddAdComponentRenderUrls(kTestAdComponentAdRenderUrl);
  auto kanon_ghost_winner =
      AuctionResult_KAnonGhostWinnerBuilder()
          .SetInterestGroupIndex(kTestIgIndex)
          .SetOwner(kTestGhostWinnerIgOwner)
          .SetOrigin(kTestGhostWinnerIgOrigin)
          .SetIgName(kTestGhostWinnerIgName)
          .SetGhostWinnerForTopLevelAuction(
              std::move(ghost_winner_for_top_level_auction))
          .SetKAnonJoinCandidates(
              KAnonJoinCandidateBuilder()
                  .SetAdRenderUrlHash(kTestAdRenderUrlHash)
                  .AddAdComponentRenderUrlsHash(kTestAdComponentRenderUrlsHash)
                  .SetReportingIdHash(kTestReportingIdHash));

  AuctionResult component_auction_result =
      AuctionResultBuilder()
          .SetTopLevelSeller(kTestTopLevelSeller)
          .SetIsChaff(false)
          .SetAuctionParams(AuctionResult_AuctionParamsBuilder()
                                .SetCiphertextGenerationId(kTestGenerationId)
                                .SetComponentSeller(kTestComponentSeller))
          .AddKAnonGhostWinners(kanon_ghost_winner);
  RawRequest raw_request = BuildTopLevelAuctionRawRequest(
      {component_auction_result}, kTestSellerSignals, kTestAuctionSignals,
      kTestPublisherHostname);
  raw_request.set_enforce_kanon(true);
  raw_request.set_num_allowed_ghost_winners(1);
  MockV8DispatchClient dispatcher;
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
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_kanon = true};
  ScoreAdsReactorTestHelper test_helper;
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  PS_VLOG(5) << "Score Ad raw response: " << raw_response.DebugString();

  ScoreAdsResponse::ScoreAdsRawResponse expected_raw_response;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ghost_winning_ad_scores {
          desirability: 1
          render: "http://ghost-winner-ad-render-url.com"
          component_renders: "http://ad-component-render-url.com"
          interest_group_name: "TestGhostWinnerIgName"
          buyer_bid: 2
          interest_group_owner: "TestGhostWinnerIgOwner"
          ad_type: AD_TYPE_PROTECTED_AUDIENCE_AD
          buyer_bid_currency: "testCurrency"
          k_anon_join_candidate {
            ad_render_url_hash: "TestAdRenderUrlHash"
            ad_component_render_urls_hash: "TestAdComponentRenderUrlsHash"
            reporting_id_hash: "TestReportingIdHash"
          }
        }
      )pb",
      &expected_raw_response));
  EXPECT_THAT(raw_response, EqualsProto(expected_raw_response));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
