// Copyright 2024 Google LLC
//
// Licensed under the Apache-form License, Version 2.0 (the "License");
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
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_helper_test_constants.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/test/mocks.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr absl::string_view kExpectedSellerDeviceSignals =
    R"JSON({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"bidCurrency":"EUR","highestScoringOtherBidCurrency":"USD","desirability":2.0,"highestScoringOtherBid":0.5})JSON";

SellerReportingDispatchRequestData GetTestDispatchRequestData(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context) {
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  PS_LOG(INFO, log_context) << "test log";
  SellerReportingDispatchRequestData reporting_dispatch_request_data = {
      .auction_config = auction_config,
      .post_auction_signals = post_auction_signals,
      .publisher_hostname = kTestPublisherHostName,
      .component_reporting_metadata = {},
      .log_context = log_context};
  return reporting_dispatch_request_data;
}

ScoreAdsResponse::AdScore GetTestWinningScoreAdsResponse() {
  ScoreAdsResponse::AdScore winning_ad_score;
  winning_ad_score.set_buyer_bid(kTestBuyerBid);
  winning_ad_score.set_buyer_bid_currency(kEurosIsoCode);
  winning_ad_score.set_interest_group_owner(kTestInterestGroupOwner);
  winning_ad_score.set_interest_group_name(kTestInterestGroupName);
  winning_ad_score.mutable_ig_owner_highest_scoring_other_bids_map()
      ->try_emplace(kTestInterestGroupOwner, google::protobuf::ListValue());
  winning_ad_score.mutable_ig_owner_highest_scoring_other_bids_map()
      ->at(kTestInterestGroupOwner)
      .add_values()
      ->set_number_value(kTestHighestScoringOtherBid);
  winning_ad_score.set_desirability(kTestDesirability);
  winning_ad_score.set_render(kTestRender);
  return winning_ad_score;
}

struct ReportResultArgIndices {
  int kAuctionConfigArgIdx =
      ReportResultArgIndex(ReportResultArgs::kAuctionConfig);
  int kSellerDeviceSignalsIdx =
      ReportResultArgIndex(ReportResultArgs::kSellerReportingSignals);
  int kDirectFromSignalsIdx =
      ReportResultArgIndex(ReportResultArgs::kDirectFromSellerSignals);
  int kAdTechCodeLoggingIdx =
      ReportResultArgIndex(ReportResultArgs::kEnableAdTechCodeLogging);
};

struct TestData {
  absl::string_view expected_auction_config = kTestAuctionConfig;
  absl::string_view expected_seller_device_signals =
      kExpectedSellerDeviceSignals;
  absl::string_view expected_direct_from_seller_signals = "{}";
  bool enable_adtech_code_logging = false;
  absl::string_view id = kTestRender;
  std::string expected_version = GetDefaultSellerUdfVersion();
  absl::string_view expected_handler_name = kReportResultEntryFunction;
  ReportResultArgIndices indexes;
};

void VerifyDispatchRequest(const TestData& test_data,
                           std::vector<DispatchRequest>& batch) {
  EXPECT_EQ(batch.size(), 1);
  std::string expected_auction_config = kTestAuctionConfig;
  EXPECT_EQ(batch[0].id, test_data.id);
  EXPECT_EQ(batch[0].version_string, test_data.expected_version);
  EXPECT_EQ(batch[0].handler_name, test_data.expected_handler_name);
  std::vector<std::shared_ptr<std::string>> response_vector = batch[0].input;
  EXPECT_EQ(*(response_vector[test_data.indexes.kAuctionConfigArgIdx]),
            test_data.expected_auction_config);
  EXPECT_EQ(*(response_vector[test_data.indexes.kSellerDeviceSignalsIdx]),
            test_data.expected_seller_device_signals);
  EXPECT_EQ(*(response_vector[test_data.indexes.kDirectFromSignalsIdx]),
            test_data.expected_direct_from_seller_signals);
  EXPECT_EQ(*(response_vector[test_data.indexes.kAdTechCodeLoggingIdx]),
            test_data.enable_adtech_code_logging ? "true" : "false");
}

TEST(TestSellerReportingManager, ReturnsRapidJsonDocOfSellerDeviceSignals) {
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestDispatchRequestData(post_auction_signals, log_context);
  PS_LOG(INFO, dispatch_request_data.log_context) << "test log";
  rapidjson::Document json_doc =
      GenerateSellerDeviceSignals(dispatch_request_data);
  absl::StatusOr<std::string> generatedjson = SerializeJsonDoc(json_doc);
  ASSERT_TRUE(generatedjson.ok());
  EXPECT_EQ(*generatedjson, kExpectedSellerDeviceSignals);
}

TEST(PerformReportResult, DispatchesRequestToReportResult) {
  auto report_result_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(kExpectedSellerDeviceSignals);
  MockCodeDispatchClient mock_dispatch_client;
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());

  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestDispatchRequestData(post_auction_signals, log_context);

  ASSERT_TRUE(document.ok()) << document.status();
  ReportingDispatchRequestConfig config;
  absl::Notification notification;

  EXPECT_CALL(mock_dispatch_client, BatchExecute)
      .WillOnce([&notification](std::vector<DispatchRequest>& batch,
                                BatchDispatchDoneCallback done_callback) {
        TestData test_data;
        VerifyDispatchRequest(test_data, batch);
        notification.Notify();
        return absl::OkStatus();
      });
  absl::Status status = PerformReportResult(
      config, *document, dispatch_request_data,
      std::move(report_result_callback), mock_dispatch_client);

  notification.WaitForNotification();
  ASSERT_TRUE(status.ok());
}

TEST(PerformReportResult, DispatchRequestFailsAndStatusNotOkReturned) {
  auto report_result_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(kExpectedSellerDeviceSignals);
  MockCodeDispatchClient mock_dispatch_client;
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());

  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestDispatchRequestData(post_auction_signals, log_context);

  ASSERT_TRUE(document.ok()) << document.status();
  ReportingDispatchRequestConfig config;
  absl::Notification notification;
  EXPECT_CALL(mock_dispatch_client, BatchExecute)
      .WillOnce([&notification](std::vector<DispatchRequest>& batch,
                                BatchDispatchDoneCallback done_callback) {
        TestData test_data;
        VerifyDispatchRequest(test_data, batch);
        notification.Notify();
        return absl::InternalError("Something went wrong");
      });
  absl::Status status = PerformReportResult(
      config, *document, dispatch_request_data,
      std::move(report_result_callback), mock_dispatch_client);

  notification.WaitForNotification();
  ASSERT_FALSE(status.ok());
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
