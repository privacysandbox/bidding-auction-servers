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

#include "absl/container/flat_hash_set.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/auction_service_integration_test_util.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/code_wrapper/buyer_reporting_test_constants.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper_test_constants.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/request_response_constants.h"
#include "src/core/test/utils/proto_test_utils.h"

using ::google::scp::core::test::EqualsProto;

namespace privacy_sandbox::bidding_auction_servers {
namespace {

PrivateAggregateContribution GetTestContribution(
    EventType event_type, absl::string_view event_name = "") {
  Bucket128Bit bucket;
  bucket.add_bucket_128_bits(0x123456789abcdf4);
  bucket.add_bucket_128_bits(0x123456789ABCDEDC);
  PrivateAggregationBucket private_aggregation_bucket;
  *private_aggregation_bucket.mutable_bucket_128_bit() = bucket;
  PrivateAggregationValue private_aggregation_value;
  private_aggregation_value.set_int_value(10);
  PrivateAggregateContribution contribution;
  *contribution.mutable_bucket() = std::move(private_aggregation_bucket);
  *contribution.mutable_value() = std::move(private_aggregation_value);
  contribution.mutable_event()->set_event_type(event_type);
  contribution.mutable_event()->set_event_name(event_name);
  return contribution;
}

class AuctionServiceReportingIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_common::log::SetGlobalPSVLogLevel(10);

    CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::OFF);
    metric::MetricContextMap<ScoreAdsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
  }
};

TEST_F(AuctionServiceReportingIntegrationTest,
       ReportingForComponentAuctionSuccessWithSellerAndBuyerCodeIsolation) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .top_level_seller = kTopLevelSeller,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation,
                          kSellerBaseCodeForComponentAuction, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  const auto& component_seller_reporting_urls =
      score_ad.win_reporting_urls().component_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_GT(score_ad.desirability(), 0);
  EXPECT_EQ(component_seller_reporting_urls.reporting_url(),
            kExpectedComponentReportResultUrl);
  EXPECT_EQ(component_seller_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(component_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedComponentReportWinUrl);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ReportingForTopLevelAuctionSuccessWithSellerAndBuyerCodeIsolation) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestComponentAuctionResultData component_data =
      GenerateTestComponentAuctionResultData();
  component_data.test_component_seller = kTestSeller;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner,
      .component_auction_data = component_data};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation,
                          kSellerBaseCodeForComponentAuction, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_seller_reporting_urls =
      score_ad.win_reporting_urls().component_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_GT(score_ad.desirability(), 0);
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kTestTopLevelReportResultUrl);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_seller_reporting_urls.reporting_url(),
            component_data.test_component_report_result_url);
  EXPECT_EQ(component_seller_reporting_urls.interaction_reporting_urls().at(
                component_data.test_component_event),
            component_data.test_component_interaction_reporting_url);

  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            component_data.test_component_win_reporting_url);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                component_data.test_component_event),
            component_data.test_component_interaction_reporting_url);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ScoresAdsReturnsReportResultUrlsWithSellerAndBuyerCodeIsolation) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation, kSellerBaseCode,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kExpectedReportResultUrl);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedReportWinUrl);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ScoresAdsSuccessWithPrivateAggregationEnabledAndNumericalContribution) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_private_aggregate_reporting = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = {},
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation,
                          kSellerBaseCodeWithPrivateAggregationNumerical,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  //   Check if the response has more than one contribution
  ASSERT_EQ(score_ad.top_level_contributions_size(), 1);
  PrivateAggregateContribution expected_contribution =
      GetTestContribution(EVENT_TYPE_WIN, "");
  expected_contribution.clear_event();
  const PrivateAggregateContribution& contribution =
      score_ad.top_level_contributions(0).contributions(0);
  EXPECT_THAT(contribution, EqualsProto(expected_contribution));
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ScoresAdsSuccessWithPrivateAggregationInReportWin) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_private_aggregate_reporting = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = {},
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithPrivateAggregation,
                          kSellerBaseCodeWithPrivateAggregationNumerical,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  PrivateAggregateContribution win_contribution =
      GetTestContribution(EVENT_TYPE_WIN, "");
  win_contribution.clear_event();
  PrivateAggregateContribution always_contribution =
      GetTestContribution(EVENT_TYPE_ALWAYS, "");
  always_contribution.clear_event();
  PrivateAggregateContribution custom_contribution =
      GetTestContribution(EVENT_TYPE_CUSTOM, "click");
  PrivateAggregateReportingResponse expected_reports;
  *expected_reports.add_contributions() = win_contribution;
  *expected_reports.add_contributions() = always_contribution;
  *expected_reports.add_contributions() = custom_contribution;
  expected_reports.set_adtech_origin(kTestSeller);
  ASSERT_EQ(score_ad.top_level_contributions_size(), 2);
  EXPECT_THAT(score_ad.top_level_contributions(0),
              EqualsProto(expected_reports));
  // The ig_idx should be set for buyer.
  for (auto& contribution : *expected_reports.mutable_contributions()) {
    contribution.set_ig_idx(1);
  }
  expected_reports.set_adtech_origin(kTestIgOwner);
  EXPECT_THAT(score_ad.top_level_contributions(1),
              EqualsProto(expected_reports));
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ScoresAdsSuccessWithPrivateAggregationInReportResult) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_private_aggregate_reporting = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = {},
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(
      runtime_config, test_score_ads_request_config,
      kTestReportWinUdfWithValidation,
      kSellerBaseCodeWithPrivateAggregationNumericalInReportResult, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  PrivateAggregateContribution win_contribution =
      GetTestContribution(EVENT_TYPE_WIN, "");
  win_contribution.clear_event();
  PrivateAggregateContribution always_contribution =
      GetTestContribution(EVENT_TYPE_ALWAYS, "");
  always_contribution.clear_event();
  PrivateAggregateContribution custom_contribution =
      GetTestContribution(EVENT_TYPE_CUSTOM, "click");
  PrivateAggregateReportingResponse expected_reports;
  *expected_reports.add_contributions() = win_contribution;
  *expected_reports.add_contributions() = always_contribution;
  *expected_reports.add_contributions() = custom_contribution;
  expected_reports.set_adtech_origin(kTestSeller);
  ASSERT_EQ(score_ad.top_level_contributions_size(), 2);
  EXPECT_THAT(score_ad.top_level_contributions(0),
              EqualsProto(expected_reports));
  expected_reports.set_adtech_origin(kTestSeller);
  EXPECT_THAT(score_ad.top_level_contributions(1),
              EqualsProto(expected_reports));
}

TEST_F(
    AuctionServiceReportingIntegrationTest,
    ScoresAdsSuccessWithPrivateAggregationEnabledAndSignalObjectsContribution) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_private_aggregate_reporting = true};
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation,
                          kSellerBaseCodeWithPrivateAggregationSignalObjects,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  //   Check if the response has more than one contribution
  ASSERT_EQ(score_ad.top_level_contributions_size(), 1);
  ASSERT_EQ(score_ad.top_level_contributions(0).contributions_size(), 2);
  const PrivateAggregateContribution& contribution_0 =
      score_ad.top_level_contributions(0).contributions(0);
  const PrivateAggregateContribution& contribution_1 =
      score_ad.top_level_contributions(0).contributions(1);

  // winning-bid = 1, scale = 1.2, offset = 100 Final value is
  // 1 * 1.2 + 100, which is 101 in integer representation.
  EXPECT_EQ(contribution_0.bucket().bucket_128_bit().bucket_128_bits(0), 101);
  EXPECT_EQ(contribution_0.bucket().bucket_128_bit().bucket_128_bits(1), 0);
  // winning-bid = 1, scale = 1.0, offset = 0 Final value is
  // 1 * 1.0 + 0, which is 1 in integer representation.
  EXPECT_EQ(contribution_0.value().int_value(), 1);

  EXPECT_EQ(contribution_1.bucket().bucket_128_bit().bucket_128_bits(0), 100);
  EXPECT_EQ(contribution_1.bucket().bucket_128_bit().bucket_128_bits(1), 0);
  EXPECT_EQ(contribution_1.value().int_value(), 200);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ScoresAdsSuccessWithProtectedAppSignalsEnabled) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_protected_app_signals = true,
      .enable_seller_and_buyer_udf_isolation = true};
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPAS(runtime_config, test_score_ads_request_config,
                           kProtectedAppSignalsBuyerBaseCode, kSellerBaseCode,
                           response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kExpectedReportResultUrl);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(
    AuctionServiceReportingIntegrationTest,
    ReportingSuccessWithSellerAndBuyerCodeIsolationAndEmptySignalsForWinner) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation,
                          kSellerBaseCodeWithNoSignalsForWinner, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kExpectedReportResultUrl);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedReportWinUrlWithNullSignalsForWinner);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().size(),
            1);
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ReportResultSuccessWhenReportWinCodeNotLoaded) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config, "",
                          kSellerBaseCodeWithNoSignalsForWinner, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  EXPECT_EQ(score_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kExpectedReportResultUrl);
  EXPECT_EQ(score_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(score_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_TRUE(score_ad.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_EQ(score_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ReportingSuccessWithCodeIsolationAndBuyerAndSellerReportingId) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .default_score_ad_version = kTestSellerCodeVersion.data(),
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .buyer_and_seller_reporting_id = kBuyerAndSellerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation, kSellerBaseCode,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kExpectedReportResultUrlWithBuyerAndSellerReportingId);
  ASSERT_TRUE(
      top_level_seller_reporting_urls.interaction_reporting_urls().contains(
          kTestInteractionEvent));
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedReportWinUrlWithBuyerAndSellerReportingId);
  ASSERT_TRUE(
      component_buyer_reporting_urls.interaction_reporting_urls().contains(
          kTestInteractionEvent));
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ReportingSuccessWhenPerBuyerSignalsAreEmpty) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  test_buyer_reporting_signals.buyer_signals = "";
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .buyer_and_seller_reporting_id = kBuyerAndSellerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation, kSellerBaseCode,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedReportWinWithEmptyPerBuyerConfig);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ResultResponseParsingFailsButReportWinSuccess) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .buyer_and_seller_reporting_id = kBuyerAndSellerReportingId,
      .interest_group_owner = kTestIgOwner};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation,
                          kSellerBaseCodeWithBadReportResult, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  ASSERT_TRUE(top_level_seller_reporting_urls.reporting_url().empty())
      << "reportResult url is expected to be empty";
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().size(),
            0);
  ASSERT_FALSE(component_buyer_reporting_urls.reporting_url().empty())
      << "reporting_url for buyer is expected not to be empty";
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().size(),
            1);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       NoReportingDoneWhenBuyerIsNotLoaded) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .buyer_and_seller_reporting_id = kBuyerAndSellerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config, "",
                          kSellerBaseCode, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  ASSERT_TRUE(component_buyer_reporting_urls.reporting_url().empty());
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().size(),
            0);
}

// runtime_config.buyers_with_report_win_enabled set is populated with the
// buyer_origin only if the reportWin endpoint was configured(not empty) for
// that buyer. This test ensures that if the set doesn't contain the
// buyer_origin of the winning buyer, reportWin is not executed.
TEST_F(AuctionServiceReportingIntegrationTest,
       NoReportWinDoneWhenBuyerDoesntHaveReportWinEnabledInRunTimeConfig) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_seller_and_buyer_udf_isolation = true};
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .buyer_and_seller_reporting_id = kBuyerAndSellerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config, "",
                          kSellerBaseCode, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(runtime_config.buyers_with_report_win_enabled.empty());
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  ASSERT_TRUE(component_buyer_reporting_urls.reporting_url().empty());
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().size(),
            0);
}

TEST_F(AuctionServiceReportingIntegrationTest,
       ReportingSuccessWithCodeIsolationAndSelectedBuyerAndSellerReportingId) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .default_score_ad_version = kTestSellerCodeVersion.data(),
      .enable_seller_and_buyer_udf_isolation = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kBuyerReportingId,
      .buyer_and_seller_reporting_id = kBuyerAndSellerReportingId,
      .selected_buyer_and_seller_reporting_id =
          kSelectedBuyerAndSellerReportingId,
      .interest_group_owner = kTestIgOwner};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidation, kSellerBaseCode,
                          response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  EXPECT_EQ(score_ad.selected_buyer_and_seller_reporting_id(),
            kSelectedBuyerAndSellerReportingId);
  EXPECT_EQ(score_ad.buyer_and_seller_reporting_id(),
            kBuyerAndSellerReportingId);
  EXPECT_EQ(score_ad.buyer_reporting_id(), kBuyerReportingId);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kExpectedReportResultUrlWithSelectedReportingId);
  ASSERT_TRUE(
      top_level_seller_reporting_urls.interaction_reporting_urls().contains(
          kTestInteractionEvent));
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedReportWinUrlWithSelectedReportingId);
  ASSERT_TRUE(
      component_buyer_reporting_urls.interaction_reporting_urls().contains(
          kTestInteractionEvent));
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(AuctionServiceReportingIntegrationTest, KAnonStatusInPAReportWin) {
  ScoreAdsResponse response;
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .default_score_ad_version = kTestSellerCodeVersion.data(),
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_kanon = true};
  runtime_config.buyers_with_report_win_enabled.insert(kTestIgOwner);
  TestBuyerReportingSignals test_buyer_reporting_signals;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .interest_group_owner = kTestIgOwner,
      .enforce_kanon = true,
      .k_anon_status = true};
  LoadAndRunScoreAdsForPA(runtime_config, test_score_ads_request_config,
                          kTestReportWinUdfWithValidationAndKAnonStatus,
                          kSellerBaseCode, response);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& score_ad = raw_response.ad_score();
  EXPECT_GT(score_ad.desirability(), 0);
  const auto& top_level_seller_reporting_urls =
      score_ad.win_reporting_urls().top_level_seller_reporting_urls();
  const auto& component_buyer_reporting_urls =
      score_ad.win_reporting_urls().buyer_reporting_urls();
  EXPECT_EQ(top_level_seller_reporting_urls.reporting_url(),
            kExpectedReportResultUrl);
  ASSERT_TRUE(
      top_level_seller_reporting_urls.interaction_reporting_urls().contains(
          kTestInteractionEvent));
  EXPECT_EQ(top_level_seller_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(component_buyer_reporting_urls.reporting_url(),
            kExpectedReportWinWithKAnonStatus);
  ASSERT_TRUE(
      component_buyer_reporting_urls.interaction_reporting_urls().contains(
          kTestInteractionEvent));
  EXPECT_EQ(component_buyer_reporting_urls.interaction_reporting_urls().at(
                kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
