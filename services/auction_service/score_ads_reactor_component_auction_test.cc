//  Copyright 2025 Google LLC
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

#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/reporting/reporting_test_util.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

// Reporting URLs
constexpr char kTestComponentReportingWinResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com&bid=2.10&modifiedBid=1.0","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"clickEvent":"http://event.com"}},"sellerLogs":["testLog"], "sellerErrors":["testLog"], "sellerWarnings":["testLog"],
"reportWinResponse":{"reportWinUrl":"http://reportWinUrl.com&bid=2.10&modifiedBid=1.0","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"clickEvent":"http://event.com"}},"buyerLogs":["testLog"], "buyerErrors":["testLog"], "buyerWarnings":["testLog"]})";
constexpr char kTestComponentReportResultUrl[] =
    "http://reportResultUrl.com&bid=2.10&modifiedBid=1.0";
constexpr char kTestComponentReportWinUrl[] =
    "http://reportWinUrl.com&bid=2.10&modifiedBid=1.0";
constexpr int kTestDesirability = 5;

// Ad score templates
constexpr char kComponentWithCurrencyAdScoreTemplate[] = R"(
      {
      "response": {
        "ad": "adMetadata",
        "desirability": $0,
        "bid": $1,
        "bidCurrency": "$2",
        "incomingBidInSellerCurrency": $3,
        "allowComponentAuction": $4
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyButNoIncomingAdScoreTemplate[] = R"(
      {
      "response": {
        "ad": "adMetadata",
        "desirability": $0,
        "bid": $1,
        "bidCurrency": "$2",
        "allowComponentAuction": $3
      },
      "logs":[]
      }
)";

constexpr char kComponentNoCurrencyAdScoreTemplate[] = R"(
      {
      "response" : {
        "ad": "adMetadata",
        "desirability" : $0,
        "bid" : $1,
        "incomingBidInSellerCurrency": $2,
        "allowComponentAuction": $3
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyAdScore[] = R"(
      {
      "response" : {
        "ad": {"key1":"adMetadata"},
        "desirability" : 0.1,
        "bid" : 0.1,
        "bidCurrency": "USD",
        "allowComponentAuction" : true
      },
      "logs":[]
      }
)";

constexpr char kComponentWithCurrencyAndIncomingAndRejectReasAdScore[] = R"(
      {
      "response" : {
        "desirability" : $0,
        "allowComponentAuction" : $1,
        "incomingBidInSellerCurrency": $2,
        "bid" : $3,
        "bidCurrency": "$4",
        "rejectReason" : "$5"
      },
      "logs":[]
      }
)";
constexpr char kSterlingIsoCode[] = "GBP";

// Debug reporting
constexpr absl::string_view kDebugLossUrlForFirstIg =
    "https://example-ssp.com/debugLoss/1";

using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

AdWithBidMetadata GetTestAdWithBidBoots() {
  std::string render_url = "bootScootin.com/render_ad?id=cowboy_boots";
  AdWithBidMetadata ad_with_bid = BuildTestAdWithBidMetadata(
      {// This must have an entry in kTestScoringSignals.
       .render_url = render_url,
       .bid = 12.15,
       .interest_group_name = "western_boot_lovers",
       .interest_group_owner = "bootScootin.com",
       .number_of_component_ads = 0,
       .make_metadata = false});

  auto* ad_map =
      ad_with_bid.mutable_ad()->mutable_struct_value()->mutable_fields();
  ad_map->try_emplace(kAdMetadataPropertyNameRenderUrl,
                      MakeAStringValue(render_url));
  ad_map->try_emplace(kAdMetadataPropertyNameMetadata,
                      MakeAListValue({
                          MakeAStringValue("french_toe"),
                          MakeAStringValue("cherry_goat"),
                          MakeAStringValue("lucchese"),
                      }));
  return ad_with_bid;
}

class ScoreAdsReactorComponentAuctionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    runtime_config_.enable_protected_app_signals = true;
    CommonTestInit();
  }

  ScoreAdsResponse ExecuteScoreAds(
      const RawRequest& raw_request, MockV8DispatchClient& dispatcher,
      const AuctionServiceRuntimeConfig& runtime_config,
      bool enable_report_result_url_generation = false) {
    ScoreAdsReactorTestHelper test_helper;
    return test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  }

  ScoreAdsResponse ExecuteScoreAds(
      const RawRequest& raw_request, MockV8DispatchClient& dispatcher,
      bool enable_report_result_url_generation = false) {
    ScoreAdsReactorTestHelper test_helper;
    return test_helper.ExecuteScoreAds(raw_request, dispatcher,
                                       runtime_config_);
  }

  AuctionServiceRuntimeConfig runtime_config_;
};

TEST_F(ScoreAdsReactorComponentAuctionTest,
       PassesTopLevelSellerToComponentAuction) {
  MockV8DispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          const auto& input = request.input;
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisherName","adComponents":["barStandardAds.com/ad_components/id=0"],"bidCurrency":"???","dataVersion":1989,"topLevelSeller":"testTopLevelSeller","renderUrl":"barStandardAds.com/render_ad?id=barbecue2"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request =
      BuildRawRequest({GetTestAdWithBidBarbecueWithComponents()},
                      {.top_level_seller = kTestTopLevelSeller});
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       PassesTopLevelSellerToPASCandidate) {
  absl::Notification finished;
  MockV8DispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([&finished](std::vector<DispatchRequest>& batch,
                            BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        const auto& req = batch[0];
        EXPECT_EQ(req.input.size(), 7);
        EXPECT_EQ(
            *req.input[static_cast<int>(ScoreAdArgs::kBidMetadata)],
            R"JSON({"interestGroupOwner":"https://PAS-Ad-Owner.com","topWindowHostname":"publisherName","bidCurrency":"USD","topLevelSeller":"testTopLevelSeller","renderUrl":"testAppAds.com/render_ad?id=bar"})JSON");
        finished.Notify();
        return absl::OkStatus();
      });
  ProtectedAppSignalsAdWithBidMetadata ad =
      GetProtectedAppSignalsAdWithBidMetadata(kTestProtectedAppSignalsRenderUrl,
                                              kTestBid);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(ad)}, {.scoring_signals = kTestProtectedAppScoringSignals,
                        .top_level_seller = kTestTopLevelSeller});
  ExecuteScoreAds(raw_request, dispatcher);
  finished.WaitForNotificationWithTimeout(absl::Seconds(1));
  ASSERT_TRUE(finished.HasBeenNotified());
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       CreatesScoresForAllAdsRequestedWithComponentAuction) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  // Currency checking will take place; these two match.
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.top_level_seller = kTestTopLevelSeller,
                       .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAdScoreTemplate, current_score++,
              1 + (std::rand() % 20), kUsdIsoCode, 3,
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.bid_currency(), kUsdIsoCode);
  EXPECT_EQ(scored_ad.incoming_bid_in_seller_currency(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
  EXPECT_GT(scored_ad.bid(), std::numeric_limits<float>::min());
}

TEST_F(ScoreAdsReactorComponentAuctionTest, CreatesScoresForPASCandidates) {
  absl::Notification finished;
  MockV8DispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        const auto& request = batch[0];
        auto incoming_bid = request.input[static_cast<int>(ScoreAdArgs::kBid)];
        float bid;
        EXPECT_TRUE(absl::SimpleAtof(*incoming_bid, &bid))
            << "Failed to convert bid to float: " << *incoming_bid;
        std::vector<absl::StatusOr<DispatchResponse>> responses;
        DispatchResponse response = {.id = request.id,
                                     .resp = absl::StrFormat(
                                         R"JSON(
                  {
                    "response": {
                      "desirability":%d,
                      "bid":%f,
                      "allowComponentAuction": true
                    }
                  })JSON",
                                         kTestDesirability, bid)};
        done_callback({response});
        return absl::OkStatus();
      });

  ProtectedAppSignalsAdWithBidMetadata protected_app_signals_ad_with_bid =
      GetProtectedAppSignalsAdWithBidMetadata(
          kTestProtectedAppSignalsRenderUrl);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(protected_app_signals_ad_with_bid)},
      {.scoring_signals = kTestProtectedAppScoringSignals,
       .top_level_seller = kTestTopLevelSeller});

  const auto& response = ExecuteScoreAds(raw_request, dispatcher);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), kTestDesirability);
  EXPECT_EQ(scored_ad.render(), kTestProtectedAppSignalsRenderUrl);
  EXPECT_EQ(scored_ad.component_renders().size(), 0);
  EXPECT_EQ(scored_ad.interest_group_name(), "");
  EXPECT_EQ(scored_ad.interest_group_owner(), kTestProtectedAppSignalsAdOwner);
  EXPECT_EQ(scored_ad.buyer_bid(), kTestBid);
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD);
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       SetsCurrencyOnAdScorefromAdWithBidForComponentAuctionAndNoModifiedBid) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  // Currency checking will take place since seller_currency set.
  // No modified bid will be set, so the original bid currency will be checked.
  RawRequest raw_request =
      BuildRawRequest({bar}, {.top_level_seller = kTestTopLevelSeller,
                              .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(
              absl::Substitute(kComponentNoCurrencyAdScoreTemplate,
                               // NOLINTNEXTLINE
                               /*desirability=*/current_score++,
                               // This is set to 0 so the buyer's (original) bid
                               // will be used. NOLINTNEXTLINE
                               /*(modified) bid=*/0,
                               // Since seller_currency is set to USD, and bar's
                               // currency is USD, this needs to match the
                               // original bid, else the AdScore will be
                               // rejected. NOLINTNEXTLINE
                               /*incomingBidInSellerCurrency=*/bar.bid(),
                               (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  // Expect USD because original AdWithBid's currency is USD.
  EXPECT_EQ(scored_ad.bid_currency(), kUsdIsoCode);
  // Bar's bid was already in USD, and the seller currency is USD, so it must be
  // bar.bid().
  EXPECT_EQ(scored_ad.incoming_bid_in_seller_currency(), bar.bid());
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
  // Modified bid set to 0, so buyer_bid should be used.
  EXPECT_EQ(scored_ad.bid(), bar.bid());
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       SendsOnlyLossDebugPingsAndPopulatesDebugUrlsForComponentAuction) {
  MockV8DispatchClient dispatcher;
  // Setting seller currency will not trigger currency checking as the AdScores
  // have no currency.
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_debug_reporting = true,
                       .top_level_seller = kTestTopLevelSeller,
                       .seller_currency = kUsdIsoCode});

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        score_logic.reserve(batch.size());
        for (int current_score = 0; current_score < batch.size();
             ++current_score) {
          score_logic.push_back(absl::StrCat(
              "{\"response\":"
              "{\"desirability\": ",
              current_score, ", \"bid\": ", current_score,
              ", \"allowComponentAuction\": true", ", \"debugReportUrls\": {",
              "    \"auctionDebugLossUrl\" : "
              "\"https://example-ssp.com/debugLoss/",
              current_score, "\",",
              "    \"auctionDebugWinUrl\" : "
              "\"https://example-ssp.com/debugWin/",
              current_score, "\"", "}}, \"logs\":[]}"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true, true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  ScoreAdsReactorTestHelper test_helper;
  EXPECT_CALL(*test_helper.async_reporter, DoReport)
      .WillOnce([](const HTTPRequest& reporting_request,
                   absl::AnyInvocable<void(absl::StatusOr<absl::string_view>)&&>
                       done_callback) {
        // Debug pings should be sent from the server for the losing
        // interest groups in component auction.
        EXPECT_EQ(reporting_request.url, kDebugLossUrlForZeroethIg);
      });
  auto response =
      test_helper.ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Debug ping should not be sent from the server for the winning interest
  // group in a component auction. Debug report urls should be present in the
  // response so that the client can send the correct debug ping depending on
  // whether this interest group wins or loses the top level auction.
  ASSERT_TRUE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_loss_url(),
            kDebugLossUrlForFirstIg);
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_win_url(),
            kDebugWinUrlForFirstIg);
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       SuccessfullyExecutesReportResultAndReportWinForComponentAuctions) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_adtech_code_logging = true,
                       .top_level_seller = kTestTopLevelSeller});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id(render id) is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting,
                       enable_report_result_win_generation](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            if (enable_report_result_win_generation) {
              response.emplace_back(kTestComponentReportingWinResponseJson);
            } else {
              response.emplace_back(kTestReportingResponseJson);
            }
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "ad":"adMetadata",
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1.0,
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestComponentReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       ParsesJsonAdMetadataInComponentAuction) {
  MockV8DispatchClient dispatcher;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.top_level_seller = kTestTopLevelSeller});

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic(batch.size(),
                                             kComponentWithCurrencyAdScore);
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()))
      << "ScoreAdsResponse could not be parsed: "
      << response.response_ciphertext();
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.ad_metadata(), "{\"key1\":\"adMetadata\"}");
}

/**
 * What's going on in this test?
 * - This is a component auction.
 * - The bids coming out of scoreAd() have no set (modified) bid.
 * - Therefore their original buyer bids will be used.
 * - Therefore the currency of those original buyer bids will be used.
 * - As with all component auctions, any adScore with a set currency must
 * undergo currency checking with the seller_currency.
 * - The seller currency set below is EUR.
 * - The AdWithBid used is bar, which has a currency of USD.
 * - So the original buyer_bid will be set on the AdScore as the modified bid,
 * and the original buyer_bid_currency (USD) will be set on the AdScore.
 * - USD is not EUR, so the AdScore will be rejected.
 */
TEST_F(ScoreAdsReactorComponentAuctionTest,
       AdScoreRejectedForMismatchedCurrencyWhenBuyerBidCurrencyUsed) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  // Currency checking will take place since seller_currency set.
  // No modified bid will be set, so the original bid currency will be checked.
  // This will cause a mismatch and a rejection.
  RawRequest raw_request = BuildRawRequest(
      {GetTestAdWithBidBar()}, {.top_level_seller = kTestTopLevelSeller,
                                .seller_currency = kEurosIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentNoCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to 0 so the buyer's (original) bid will be used.
              /*(modified) bid=*/0,
              // Since seller_currency is set to EUR, and bar's currency is USD,
              // scoreAd() is responsible for setting this correctly,
              // but B&A can't verify that it is set correctly.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/1.776,
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       BidCurrencyCanBeModifiedWhenNoSellerCurrencySet) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Seller currency omitted so currency checking will NOT take place.
  RawRequest raw_request = BuildRawRequest(
      {GetTestAdWithBidBar()}, {.top_level_seller = kTestTopLevelSeller});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to a positive and nonzero value so it will be used.
              /*(modified) bid=*/1.689,
              // Setting this to EUR is fine as no seller_currency is set.
              // NOLINTNEXTLINE
              /*bidCurrency=*/kEuroIsoCode,
              // Since seller_currency is not set, this is ignored.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/1.776,
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_FLOAT_EQ(scored_ad.bid(), 1.689);
  // Expect EUR even though original AdWithBid's currency is USD, as this is
  // what scoreAd() chose to set the currency to.
  EXPECT_EQ(scored_ad.bid_currency(), kEuroIsoCode);
  EXPECT_FLOAT_EQ(scored_ad.incoming_bid_in_seller_currency(), 1.776);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       BidRejectedForModifiedIncomingBidInSellerCurrency) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  // Seller currency set to USD.
  // Currency checking will take place since seller_currency is set.
  RawRequest raw_request =
      BuildRawRequest({bar}, {.top_level_seller = kTestTopLevelSeller,
                              .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentNoCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to 0 so the buyer's (original) bid will be used.
              /*(modified) bid=*/0,
              // Since seller_currency is set to USD, and bar's currency is USD,
              // this needs to match the original bid, else the AdScore
              // will be rejected.
              // We are deliberately setting it wrong so it is rejected.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/bar.bid() + 1,
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       BidWithMismatchedModifiedCurrencyAcceptedSinceModifiedBidNotSet) {
  MockV8DispatchClient dispatcher;
  // Desirability must be greater than 0 to count a winner.
  // Initialize to 1 so first bit could win if it's the only bid.
  int current_score = 1;
  bool allowComponentAuction = true;
  // Bar's bid currency is USD
  AdWithBidMetadata bar = GetTestAdWithBidBar();
  // Seller currency omitted so currency checking will NOT take place.
  RawRequest raw_request =
      BuildRawRequest({bar}, {.top_level_seller = kTestTopLevelSeller,
                              .seller_currency = kUsdIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad,
                       &bar](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAdScoreTemplate,
              // NOLINTNEXTLINE
              /*desirability=*/current_score++,
              // This is set to 0 so the buyer's (original) bid will be used.
              /*(modified) bid=*/0,
              // Setting this to EUR would normally fail currency checking,
              // but in this case the adScore will pass
              // because EUR will be IGNORED as it is the currency
              // of the modified bid, and the modified bid was not set.
              // NOLINTNEXTLINE
              /*bidCurrency=*/kEuroIsoCode,
              // Since seller_currency is set, this must match the original bid.
              // NOLINTNEXTLINE
              /*incomingBidInSellerCurrency=*/bar.bid(),
              (allowComponentAuction) ? "true" : "false"));
          // Component auction and modified bid of 0 means
          // device will receive the original bid.
          // No explicitly set bid currency means that
          // the original bid's currency must be used
          // and will be set in the reactor here.
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  // Expect USD because original AdWithBid's currency is USD.
  EXPECT_EQ(scored_ad.bid_currency(), kUsdIsoCode);
  // USD->USD means no translation.
  EXPECT_EQ(scored_ad.incoming_bid_in_seller_currency(), bar.bid());
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "\"adMetadata\"");
  // Modified bid not set, so buyer_bid used.
  EXPECT_EQ(scored_ad.bid(), bar.bid());
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       ModifiedBidOnScoreRejectedForCurrencyMismatch) {
  MockV8DispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  // EUR will not match the AdScore.bid_currency of USD.
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.top_level_seller = kTestTopLevelSeller,
                       .seller_currency = kEuroIsoCode});
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyButNoIncomingAdScoreTemplate,
              // USD will not match the seller_currency of EUR.
              current_score++, 1 + (std::rand() % 20), kUsdIsoCode,
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorComponentAuctionTest, RespectConfiguredDebugUrlLimits) {
  server_common::log::SetGlobalPSVLogLevel(10);
  MockV8DispatchClient dispatcher;
  RawRequest raw_request =
      BuildRawRequest({BuildTestAdWithBidMetadata(), GetTestAdWithBidBar()},
                      {.enable_debug_reporting = true,
                       .top_level_seller = kTestTopLevelSeller});
  std::string long_win_url(1024, 'A');
  std::string long_loss_url(1025, 'B');

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([long_win_url, long_loss_url](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.reserve(batch.size());
        for (int i = 0; i < batch.size(); ++i) {
          score_logic.emplace_back(absl::Substitute(
              R"JSON(
                  {
                  "response" : {
                    "desirability" : $0,
                    "bid" : 1,
                    "allowComponentAuction" : true,
                    "debugReportUrls": {
                      "auctionDebugLossUrl": "$1",
                      "auctionDebugWinUrl": "$2"
                    }
                  },
                  "logs":[]
                  }
                )JSON",
              batch.size() - i, long_loss_url, long_win_url));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic));
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true,
      .max_allowed_size_all_debug_urls_kb = 1};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  PS_VLOG(5) << "Response:\n" << scored_ad.DebugString();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  ASSERT_TRUE(scored_ad.has_debug_report_urls());
  EXPECT_FALSE(scored_ad.debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_win_url().size(), 1024);
  EXPECT_TRUE(scored_ad.debug_report_urls().auction_debug_loss_url().empty());
}

// This test case validates that reject reason is returned in response of
// ScoreAds for ads where it was of any valid value other than 'not-available'.
TEST_F(ScoreAdsReactorComponentAuctionTest,
       CaptureRejectionReasonsForRejectedAds) {
  MockV8DispatchClient dispatcher;
  bool allowComponentAuction = true;
  // Each ad must have its own unique render url, lest the IDs clash during roma
  // batch execution and one gets clobbered.
  AdWithBidMetadata foo, bar, barbecue, boots;
  // AdWithBid Foo is to be rejected by the scoreAd() code, which will delcare
  // it invalid.
  foo = BuildTestAdWithBidMetadata();
  // AdWithBid Bar has bid currency USD. Since this matches the USD set as the
  // seller currency below, the AdScore for bar must have an
  // incomingBidInSellerCurrency which is unmodified (that is, which matches
  // bar.bid()). This test will deliberately violate this requirement below to
  // test the seller rejection reason setting.
  bar = GetTestAdWithBidBar();
  // This AdWithBid is intended to be the sole non-rejected AwB.
  barbecue = GetTestAdWithBidBarbecue();
  // Boots's modified bid currency of Sterling should clash with the stated
  // auction currency of USD below, leading to a seller rejection reason of
  // currency mismatch.
  boots = GetTestAdWithBidBoots();

  RawRequest raw_request = BuildRawRequest(
      {foo, bar, barbecue, boots}, {.top_level_seller = kTestTopLevelSeller,
                                    .seller_currency = kUsdIsoCode});

  ASSERT_EQ(raw_request.ad_bids_size(), 4);

  absl::flat_hash_map<std::string, std::string> id_to_rejection_reason;
  // Make sure id is tracked to render urls to stay unique.
  // Bar is to have its rejection reason assigned by the reactor code for
  // incomingBidInSellerCurrency illegal modification.
  id_to_rejection_reason.insert_or_assign(bar.render(), "not-available");
  id_to_rejection_reason.insert_or_assign(foo.render(), "invalid-bid");
  // barbecue is to survive unscathed and should win the auction.
  id_to_rejection_reason.insert_or_assign(barbecue.render(), "not-available");
  id_to_rejection_reason.insert_or_assign(boots.render(), "not-available");

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&allowComponentAuction, &id_to_rejection_reason, &bar,
                       &boots](std::vector<DispatchRequest>& batch,
                               BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          std::string rejection_reason = id_to_rejection_reason.at(request.id);
          score_logic.push_back(absl::Substitute(
              kComponentWithCurrencyAndIncomingAndRejectReasAdScore,
              // Disable this check because all args to
              // absl::Substitute() are named generically, so explicitly calling
              // out "desirability" and "bid" runs afoul of these checks.
              // NOLINTNEXTLINE
              /*desirability=*/1, (allowComponentAuction) ? "true" : "false",
              // IG Bar's bid currency is USD. As Seller currency is USD, bar's
              // incomingBidInSellerCurrency must equal the original bid or the
              // bid should be rejected for invalid incomingBidInSellerCurrency.
              // So here, we deliberately set
              // barAdScore.incomingBidInSellerCurrency to NOT bar.bid(), so
              // that bar is rejected and we can test its reject reason. The
              // other AdScores just don't have incomingBidInSellerCurrency set
              // on them (0 is default).
              (request.id == bar.render()) ? bar.bid() + 1689 : 0.0f,
              // NOLINTNEXTLINE
              /*bid=*/1 + (std::rand() % 20),
              // Only the boots AwB shall be rejected for currency mismatch.
              // NOLINTNEXTLINE
              /*bidCurrency=*/
              (request.id == boots.render()) ? kSterlingIsoCode : "",
              rejection_reason));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Assertion avoids out-of-bounds array accesses below.
  ASSERT_EQ(scored_ad.ad_rejection_reasons().size(), 3);

  int num_matching = 0;
  // Note that the batch responses can be in any order.
  for (const auto& ad_rejection_reason : scored_ad.ad_rejection_reasons()) {
    PS_VLOG(5) << "Found ad rejection reason: "
               << ad_rejection_reason.DebugString();
    if (ad_rejection_reason.interest_group_name() ==
        boots.interest_group_name()) {
      ++num_matching;
      EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
                boots.interest_group_owner());
      EXPECT_EQ(ad_rejection_reason.rejection_reason(),
                SellerRejectionReason::BID_FROM_SCORE_AD_FAILED_CURRENCY_CHECK);
    } else if (ad_rejection_reason.interest_group_name() ==
               bar.interest_group_name()) {
      ++num_matching;
      EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
                bar.interest_group_owner());
      EXPECT_EQ(ad_rejection_reason.rejection_reason(),
                SellerRejectionReason::INVALID_BID);
    } else if (ad_rejection_reason.interest_group_name() ==
               foo.interest_group_name()) {
      ++num_matching;
      EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
                foo.interest_group_owner());
      EXPECT_EQ(ad_rejection_reason.rejection_reason(),
                SellerRejectionReason::INVALID_BID);
    }
  }
  ASSERT_EQ(num_matching, 3);
}

TEST_F(ScoreAdsReactorComponentAuctionTest,
       SkipsPASCandidatesIfAllowComponentAuctionFalse) {
  absl::Notification finished;
  MockV8DispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        const auto& request = batch[0];
        auto incoming_bid = request.input[static_cast<int>(ScoreAdArgs::kBid)];
        float bid;
        EXPECT_TRUE(absl::SimpleAtof(*incoming_bid, &bid))
            << "Failed to convert bid to float: " << *incoming_bid;
        std::vector<absl::StatusOr<DispatchResponse>> responses;
        DispatchResponse response = {.id = request.id,
                                     .resp = absl::StrFormat(
                                         R"JSON(
                  {
                    "response": {
                      "desirability":%d,
                      "bid":%f,
                      "allowComponentAuction": false
                    }
                  })JSON",
                                         kTestDesirability, bid)};
        done_callback({response});
        return absl::OkStatus();
      });

  ProtectedAppSignalsAdWithBidMetadata protected_app_signals_ad_with_bid =
      GetProtectedAppSignalsAdWithBidMetadata(
          kTestProtectedAppSignalsRenderUrl);
  RawRequest raw_request = BuildProtectedAppSignalsRawRequest(
      {std::move(protected_app_signals_ad_with_bid)},
      {.scoring_signals = kTestProtectedAppScoringSignals,
       .top_level_seller = kTestTopLevelSeller});

  const auto& response = ExecuteScoreAds(raw_request, dispatcher);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), 0);
  EXPECT_EQ(scored_ad.render(), "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
