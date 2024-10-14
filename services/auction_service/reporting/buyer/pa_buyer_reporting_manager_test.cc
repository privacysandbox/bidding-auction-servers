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
#include "services/auction_service/reporting/buyer/pa_buyer_reporting_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
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
constexpr absl::string_view kInvalidUrl =
    "example.com:80:https://path/to/2?query=1&2#fragment";

struct ReportWinArgIndices {
  int kAuctionConfigArgIdx =
      PAReportWinArgIndex(PAReportWinArgs::kAuctionConfig);
  int kPerBuyerSignalsArgIdx =
      PAReportWinArgIndex(PAReportWinArgs::kPerBuyerSignals);
  int kSignalsForWinnerArgIdx =
      PAReportWinArgIndex(PAReportWinArgs::kSignalsForWinner);
  int kBuyerReportingSignalsArgIdx =
      PAReportWinArgIndex(PAReportWinArgs::kBuyerReportingSignals);
  int kDirectFromSellerSignalsArgIdx =
      PAReportWinArgIndex(PAReportWinArgs::kDirectFromSellerSignals);
  int kEnableLoggingArgIdx =
      PAReportWinArgIndex(PAReportWinArgs::kEnableLogging);
};

struct TestData {
  std::string expected_auction_config = kTestAuctionConfig;
  std::string expected_seller_device_signals;
  std::string expected_direct_from_seller_signals = "{}";
  bool enable_adtech_code_logging = false;
  absl::string_view id = kTestRender;
  std::string expected_handler_name = kReportWinEntryFunction;
  ReportWinArgIndices indexes;
  std::string expected_buyer_signals = kTestBuyerSignals;
  std::string expected_version;
};

void VerifyDispatchRequest(
    const TestData& test_data,
    const BuyerReportingDispatchRequestData& dispatch_request_data,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data,
    std::vector<DispatchRequest>& batch) {
  EXPECT_EQ(batch.size(), 1);
  std::string expected_auction_config = kTestAuctionConfig;
  EXPECT_EQ(batch[0].id, test_data.id);
  EXPECT_EQ(batch[0].version_string, test_data.expected_version);
  EXPECT_EQ(batch[0].handler_name, test_data.expected_handler_name);
  std::vector<std::shared_ptr<std::string>> response_vector = batch[0].input;
  EXPECT_EQ(
      *(response_vector[PAReportWinArgIndex(PAReportWinArgs::kAuctionConfig)]),
      test_data.expected_auction_config);
  EXPECT_EQ(*(response_vector[PAReportWinArgIndex(
                PAReportWinArgs::kPerBuyerSignals)]),
            test_data.expected_buyer_signals);
  EXPECT_EQ(*(response_vector[PAReportWinArgIndex(
                PAReportWinArgs::kDirectFromSellerSignals)]),
            test_data.expected_direct_from_seller_signals);
  EXPECT_EQ(
      *(response_vector[PAReportWinArgIndex(PAReportWinArgs::kEnableLogging)]),
      test_data.enable_adtech_code_logging ? "true" : "false");
  VerifyPABuyerReportingSignalsJson(
      *(response_vector[PAReportWinArgIndex(
          PAReportWinArgs::kBuyerReportingSignals)]),
      dispatch_request_data, seller_dispatch_request_data);
}

class PerformReportWin : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(PerformReportWin, DispatchesRequestToReportWin) {
  auto report_win_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  SellerReportingDispatchRequestData seller_dispatch_request_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);

  rapidjson::Document document =
      GenerateTestSellerDeviceSignals(seller_dispatch_request_data);

  BuyerReportingDispatchRequestData dispatch_request_data =
      GetTestBuyerDispatchRequestData(log_context);

  ReportingDispatchRequestConfig config;

  absl::StatusOr<std::string> expected_version = GetBuyerReportWinVersion(
      dispatch_request_data.buyer_origin, AuctionType::kProtectedAudience);
  ASSERT_TRUE(expected_version.ok());
  TestData test_data = {
      .expected_version = *expected_version,
  };

  absl::Notification notification;
  MockV8DispatchClient mock_dispatch_client;
  EXPECT_CALL(mock_dispatch_client, BatchExecute)
      .WillOnce([&notification, &test_data, &dispatch_request_data,
                 &seller_dispatch_request_data](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback done_callback) {
        VerifyDispatchRequest(test_data, dispatch_request_data,
                              seller_dispatch_request_data, batch);
        notification.Notify();
        return absl::OkStatus();
      });
  absl::Status status =
      PerformPAReportWin(config, dispatch_request_data, document,
                         std::move(report_win_callback), mock_dispatch_client);

  notification.WaitForNotification();
  ASSERT_TRUE(status.ok());
}

TEST_F(PerformReportWin, DispatchRequestFailsAndStatusNotOkReturned) {
  auto report_win_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  SellerReportingDispatchRequestData seller_dispatch_request_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);

  rapidjson::Document document =
      GenerateTestSellerDeviceSignals(seller_dispatch_request_data);

  BuyerReportingDispatchRequestData dispatch_request_data =
      GetTestBuyerDispatchRequestData(log_context);

  ReportingDispatchRequestConfig config;

  absl::StatusOr<std::string> expected_version = GetBuyerReportWinVersion(
      dispatch_request_data.buyer_origin, AuctionType::kProtectedAudience);
  ASSERT_TRUE(expected_version.ok());
  TestData test_data = {
      .expected_version = *expected_version,
  };

  absl::Notification notification;
  MockV8DispatchClient mock_dispatch_client;
  EXPECT_CALL(mock_dispatch_client, BatchExecute)
      .WillOnce([&notification, &test_data, &dispatch_request_data,
                 &seller_dispatch_request_data](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback done_callback) {
        VerifyDispatchRequest(test_data, dispatch_request_data,
                              seller_dispatch_request_data, batch);
        notification.Notify();
        return absl::InternalError("Something went wrong");
      });
  absl::Status status =
      PerformPAReportWin(config, dispatch_request_data, document,
                         std::move(report_win_callback), mock_dispatch_client);

  notification.WaitForNotification();
  ASSERT_FALSE(status.ok());
}

TEST_F(PerformReportWin, ReportWinNotExecutedWhenVersionLookupFails) {
  auto report_win_callback =
      [](const std::vector<absl::StatusOr<DispatchResponse>>& result) {};
  RequestLogContext log_context({},
                                server_common::ConsentedDebugConfiguration());
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals =
      GeneratePostAuctionSignals(winning_ad_score, kUsdIsoCode);
  SellerReportingDispatchRequestData seller_dispatch_request_data =
      GetTestSellerDispatchRequestData(post_auction_signals, log_context);

  rapidjson::Document document =
      GenerateTestSellerDeviceSignals(seller_dispatch_request_data);

  BuyerReportingDispatchRequestData dispatch_request_data =
      GetTestBuyerDispatchRequestData(log_context);

  ReportingDispatchRequestConfig config;
  dispatch_request_data.buyer_origin = kInvalidUrl;
  MockV8DispatchClient mock_dispatch_client;
  EXPECT_CALL(mock_dispatch_client, BatchExecute).Times(0);
  absl::Status status =
      PerformPAReportWin(config, dispatch_request_data, document,
                         std::move(report_win_callback), mock_dispatch_client);
  ASSERT_FALSE(status.ok());
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
