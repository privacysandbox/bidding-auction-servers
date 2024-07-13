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

#include <thread>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/auction_service_integration_test_util.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper_test_constants.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr absl::string_view kExpectedReportResultUrl =
    "http://"
    "test.com&bid=1&bidCurrency=EUR&highestScoringOtherBid=0&"
    "highestScoringOtherBidCurrency=???&topWindowHostname=fenceStreetJournal."
    "com&interestGroupOwner=barStandardAds.com";
constexpr char kTestIgOwner[] = "barStandardAds.com";
class AuctionServiceReportingIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::OFF);
    metric::MetricContextMap<ScoreAdsRequest>(
        server_common::telemetry::BuildDependentConfig(config_proto));
  }
};

TEST_F(AuctionServiceReportingIntegrationTest,
       ScoresAdsReturnsReportResultUrlsWithSellerAndBuyerCodeIsolation) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_input_noising = false,
      .enable_seller_and_buyer_udf_isolation = true};
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kExpectedReportResultUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_TRUE(scoredAd.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
