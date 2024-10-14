
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
#include "services/auction_service/reporting/reporting_test_util.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/json_util.h"
namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr absl::string_view kExpectedSellerDeviceSignals =
    R"JSON({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"bidCurrency":"EUR","highestScoringOtherBidCurrency":"USD","desirability":2.0,"highestScoringOtherBid":0.5})JSON";

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
rapidjson::Document GetReportResultJsonObj(
    const ReportResultResponse& report_result_response) {
  rapidjson::Document document(rapidjson::kObjectType);
  rapidjson::Value signals_for_winner_val(
      report_result_response.signals_for_winner.c_str(),
      document.GetAllocator());
  document.AddMember(kSignalsForWinner, signals_for_winner_val,
                     document.GetAllocator());
  rapidjson::Value report_result_url(
      report_result_response.report_result_url.c_str(),
      document.GetAllocator());
  document.AddMember(kReportResultUrl, report_result_url.Move(),
                     document.GetAllocator());
  document.AddMember(kSendReportToInvoked,
                     report_result_response.send_report_to_invoked,
                     document.GetAllocator());
  document.AddMember(kRegisterAdBeaconInvoked,
                     report_result_response.register_ad_beacon_invoked,
                     document.GetAllocator());
  rapidjson::Value map_value(rapidjson::kObjectType);
  for (const auto& [event, url] :
       report_result_response.interaction_reporting_urls) {
    rapidjson::Value interaction_event(event.c_str(), document.GetAllocator());
    rapidjson::Value interaction_url(url.c_str(), document.GetAllocator());
    map_value.AddMember(interaction_event.Move(), interaction_url.Move(),
                        document.GetAllocator());
  }
  document.AddMember(kInteractionReportingUrlsWrapperResponse, map_value,
                     document.GetAllocator());
  return document;
}
void TestResponse(const ReportResultResponse& report_result_response,
                  const ReportResultResponse& expected_response) {
  EXPECT_EQ(report_result_response.report_result_url,
            expected_response.report_result_url);
  EXPECT_EQ(report_result_response.interaction_reporting_urls.size(),
            expected_response.interaction_reporting_urls.size());
  EXPECT_EQ(report_result_response.signals_for_winner,
            expected_response.signals_for_winner);
}
absl::StatusOr<std::string> BuildJsonObject(
    const ReportResultResponse& response,
    const ReportingResponseLogs& console_logs,
    const ReportingDispatchRequestConfig& config) {
  rapidjson::Document outerDoc(rapidjson::kObjectType);
  rapidjson::Document report_result_obj = GetReportResultJsonObj(response);
  if (config.enable_adtech_code_logging) {
    SetAdTechLogs(console_logs, outerDoc);
  }
  outerDoc.AddMember(kResponse, report_result_obj, outerDoc.GetAllocator());
  return SerializeJsonDoc(outerDoc);
}

class SellerReportingManagerTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(SellerReportingManagerTest, ReturnsRapidJsonDocOfSellerDeviceSignals) {
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
  rapidjson::Document json_doc =
      GenerateTestSellerDeviceSignals(dispatch_request_data);
  absl::StatusOr<std::string> generatedjson = SerializeJsonDoc(json_doc);
  ASSERT_TRUE(generatedjson.ok());
  EXPECT_EQ(*generatedjson, kExpectedSellerDeviceSignals);
}

TEST_F(SellerReportingManagerTest,
       PerformReportResult_DispatchesRequestToReportResult) {
  auto report_result_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  MockV8DispatchClient mock_dispatch_client;
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
  rapidjson::Document document =
      GenerateTestSellerDeviceSignals(dispatch_request_data);
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
      config, document, dispatch_request_data,
      std::move(report_result_callback), mock_dispatch_client);
  notification.WaitForNotification();
  ASSERT_TRUE(status.ok());
}
TEST_F(SellerReportingManagerTest,
       PerformReportResult_DispatchRequestFailsAndStatusNotOkReturned) {
  auto report_result_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(kExpectedSellerDeviceSignals);
  MockV8DispatchClient mock_dispatch_client;
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);
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
TEST_F(SellerReportingManagerTest,
       ParseReportResultResponse_ParsesReportResultResponseSuccessfully) {
  server_common::log::SetGlobalPSVLogLevel(10);
  ReportResultResponse expected_response{
      .report_result_url = kTestReportResultUrl,
      .send_report_to_invoked = kSendReportToInvokedTrue,
      .register_ad_beacon_invoked = kRegisterAdBeaconInvokedTrue,
      .signals_for_winner = kTestSignalsForWinner};
  expected_response.interaction_reporting_urls.try_emplace(
      kTestInteractionEvent, kTestInteractionUrl);
  ReportingDispatchRequestConfig config = {.enable_adtech_code_logging = true};
  ReportingResponseLogs console_logs;
  console_logs.logs.emplace_back(kTestLog);
  console_logs.warnings.emplace_back(kTestLog);
  console_logs.errors.emplace_back(kTestLog);
  absl::StatusOr<std::string> json_string =
      BuildJsonObject(expected_response, console_logs, config);
  ASSERT_TRUE(json_string.ok()) << json_string.status();
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  absl::StatusOr<ReportResultResponse> response =
      ParseReportResultResponse(config, json_string.value(), log_context);
  TestResponse(response.value(), expected_response);
}
TEST_F(SellerReportingManagerTest,
       ParseReportResultResponse_ParsingFailureReturnsNoOkStatus) {
  std::string bad_json = "{abc:def hij:klm";
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  absl::StatusOr<ReportResultResponse> response =
      ParseReportResultResponse({}, bad_json, log_context);
  EXPECT_FALSE(response.ok());
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
