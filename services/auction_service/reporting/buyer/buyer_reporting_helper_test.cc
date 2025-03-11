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
#include "services/auction_service/reporting/buyer/buyer_reporting_helper.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/private_aggregation/private_aggregation_manager_test_util.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/reporting/reporting_test_util.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

rapidjson::Document GetReportWinJsonObj(
    const ReportWinResponse& report_win_response) {
  rapidjson::Document document(rapidjson::kObjectType);
  rapidjson::Value report_win_url(report_win_response.report_win_url.c_str(),
                                  document.GetAllocator());
  document.AddMember(kReportWinUrl, report_win_url.Move(),
                     document.GetAllocator());
  document.AddMember(kSendReportToInvoked,
                     report_win_response.send_report_to_invoked,
                     document.GetAllocator());
  document.AddMember(kRegisterAdBeaconInvoked,
                     report_win_response.register_ad_beacon_invoked,
                     document.GetAllocator());
  rapidjson::Value map_value(rapidjson::kObjectType);
  for (const auto& [event, url] :
       report_win_response.interaction_reporting_urls) {
    rapidjson::Value interaction_event(event.c_str(), document.GetAllocator());
    rapidjson::Value interaction_url(url.c_str(), document.GetAllocator());
    map_value.AddMember(interaction_event.Move(), interaction_url.Move(),
                        document.GetAllocator());
  }
  document.AddMember(kInteractionReportingUrlsWrapperResponse, map_value,
                     document.GetAllocator());
  return document;
}

absl::StatusOr<std::string> BuildJsonObject(
    const ReportWinResponse& response,
    const ReportingResponseLogs& console_logs,
    const ReportingDispatchRequestConfig& config) {
  rapidjson::Document outerDoc(rapidjson::kObjectType);
  rapidjson::Document report_win_obj = GetReportWinJsonObj(response);
  if (config.enable_adtech_code_logging) {
    SetAdTechLogs(console_logs, outerDoc);
  }
  outerDoc.AddMember(kResponse, report_win_obj, outerDoc.GetAllocator());
  return SerializeJsonDoc(outerDoc);
}

absl::StatusOr<std::string> BuildJsonObjectWithPrivateAggregateContributions(
    const ReportWinResponse& response,
    const ReportingResponseLogs& console_logs,
    const ReportingDispatchRequestConfig& config) {
  rapidjson::Document outerDoc(rapidjson::kObjectType);
  rapidjson::Document report_win_obj = GetReportWinJsonObj(response);
  if (config.enable_adtech_code_logging) {
    SetAdTechLogs(console_logs, outerDoc);
  }
  std::vector<PrivateAggregateContribution> win_contributions;
  std::vector<PrivateAggregateContribution> loss_contributions;
  std::vector<PrivateAggregateContribution> always_contributions;
  std::vector<PrivateAggregateContribution> custom_contributions;
  for (const auto& contribution : response.pagg_response.contributions()) {
    if (contribution.event().event_type() == EVENT_TYPE_WIN) {
      win_contributions.push_back(contribution);
    } else if (contribution.event().event_type() == EVENT_TYPE_LOSS) {
      loss_contributions.push_back(contribution);
    } else if (contribution.event().event_type() == EVENT_TYPE_ALWAYS) {
      always_contributions.push_back(contribution);
    } else if (contribution.event().event_type() == EVENT_TYPE_CUSTOM) {
      custom_contributions.push_back(contribution);
    }
  }
  absl::flat_hash_map<std::string, std::vector<PrivateAggregateContribution>>
      custom_event_contributions;
  custom_event_contributions.try_emplace(kTestCustomEvent,
                                         custom_contributions);
  rapidjson::Document paapi_contributions_obj =
      CreateContributionResponseDocument(win_contributions, loss_contributions,
                                         always_contributions,
                                         custom_event_contributions);
  outerDoc.AddMember("paapiContributions", paapi_contributions_obj,
                     outerDoc.GetAllocator());
  outerDoc.AddMember(kResponse, report_win_obj, outerDoc.GetAllocator());
  return SerializeJsonDoc(outerDoc);
}

void TestResponse(const ReportWinResponse& report_win_response,
                  const ReportWinResponse& expected_response) {
  EXPECT_EQ(report_win_response.report_win_url,
            expected_response.report_win_url);
  EXPECT_EQ(report_win_response.interaction_reporting_urls.size(),
            expected_response.interaction_reporting_urls.size());
  google::protobuf::util::MessageDifferencer diff;
  std::string diff_output;
  diff.ReportDifferencesToString(&diff_output);
  EXPECT_TRUE(diff.Compare(report_win_response.pagg_response,
                           expected_response.pagg_response))
      << diff_output;
}

void VerifyPABuyerReportingSignalsJson(
    std::shared_ptr<std::string>& buyer_reporting_signals_json,
    const BuyerReportingDispatchRequestData&
        expected_buyer_dispatch_request_data) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData reporting_dispatch_data{.log_context =
                                                                log_context};
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(*buyer_reporting_signals_json);
  ASSERT_TRUE(document.ok());
  ParseBuyerReportingSignals(reporting_dispatch_data, *document);
  VerifyBuyerReportingSignals(reporting_dispatch_data,
                              expected_buyer_dispatch_request_data);
}

BuyerReportingDispatchRequestData GetTestBuyerReportingDispatchRequestData(
    RequestLogContext& log_context) {
  return {.buyer_signals = kTestBuyerSignalsObj,
          .join_count = kTestJoinCount,
          .recency = kTestRecency,
          .modeling_signals = kTestModelingSignals,
          .seller = kTestSeller.data(),
          .interest_group_name = kTestInterestGroupName.data(),
          .ad_cost = kTestAdCost,
          .data_version = kTestDataVersion,
          .made_highest_scoring_other_bid = true,
          .log_context = log_context};
}

TEST(GetBuyerDeviceSignals, ReturnsBuyerReportingSignalsWithBuyerReportingId) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData reporting_dispatch_data =
      GetTestBuyerReportingDispatchRequestData(log_context);
  reporting_dispatch_data.buyer_reporting_id = kTestBuyerReportingId;
  reporting_dispatch_data.interest_group_name = "";
  rapidjson::Document document(rapidjson::kObjectType);
  absl::StatusOr<std::shared_ptr<std::string>> buyer_device_signals =
      GenerateBuyerDeviceSignals(reporting_dispatch_data, document);
  ASSERT_TRUE(buyer_device_signals.ok());
  VerifyPABuyerReportingSignalsJson(*buyer_device_signals,
                                    reporting_dispatch_data);
}

TEST(GetBuyerDeviceSignals,
     ReturnsBuyerReportingSignalsWithBuyerAndSellerReportingId) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData reporting_dispatch_data =
      GetTestBuyerReportingDispatchRequestData(log_context);
  reporting_dispatch_data.interest_group_name = "";
  reporting_dispatch_data.buyer_and_seller_reporting_id =
      kTestBuyerAndSellerReportingId;
  rapidjson::Document document(rapidjson::kObjectType);
  absl::StatusOr<std::shared_ptr<std::string>> buyer_device_signals =
      GenerateBuyerDeviceSignals(reporting_dispatch_data, document);
  ASSERT_TRUE(buyer_device_signals.ok());
  VerifyPABuyerReportingSignalsJson(*buyer_device_signals,
                                    reporting_dispatch_data);
}

TEST(GetBuyerDeviceSignals, ReturnsBuyerReportingSignalsForComponentAuctions) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData reporting_dispatch_data =
      GetTestBuyerReportingDispatchRequestData(log_context);
  rapidjson::Document document(rapidjson::kObjectType);
  document.AddMember(kModifiedBid, 1.0, document.GetAllocator());
  absl::StatusOr<std::shared_ptr<std::string>> buyer_device_signals =
      GenerateBuyerDeviceSignals(reporting_dispatch_data, document);
  ASSERT_TRUE(buyer_device_signals.ok()) << buyer_device_signals.status();
  VerifyPABuyerReportingSignalsJson(*buyer_device_signals,
                                    reporting_dispatch_data);
}

TEST(GetBuyerDeviceSignals, ReturnsBuyerReportingSignalsWithIGName) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData reporting_dispatch_data =
      GetTestBuyerReportingDispatchRequestData(log_context);
  rapidjson::Document document(rapidjson::kObjectType);
  absl::StatusOr<std::shared_ptr<std::string>> buyer_device_signals =
      GenerateBuyerDeviceSignals(reporting_dispatch_data, document);
  ASSERT_TRUE(buyer_device_signals.ok());
  VerifyPABuyerReportingSignalsJson(*buyer_device_signals,
                                    reporting_dispatch_data);
}

TEST(GetBuyerDeviceSignals, ReturnsBuyerReportingSignalsWithKAnonStatus) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  server_common::log::SetGlobalPSVLogLevel(20);
  BuyerReportingDispatchRequestData reporting_dispatch_data =
      GetTestBuyerReportingDispatchRequestData(log_context);
  reporting_dispatch_data.k_anon_status = kTestKAnonStatus;
  rapidjson::Document document(rapidjson::kObjectType);
  absl::StatusOr<std::shared_ptr<std::string>> buyer_device_signals =
      GenerateBuyerDeviceSignals(reporting_dispatch_data, document);
  ASSERT_TRUE(buyer_device_signals.ok());
  VerifyPABuyerReportingSignalsJson(*buyer_device_signals,
                                    reporting_dispatch_data);
}

TEST(ParseReportResultResponse, ParsesReportWinResponseSuccessfully) {
  server_common::log::SetGlobalPSVLogLevel(10);
  ReportWinResponse expected_response{
      .report_win_url = kTestReportResultUrl,
      .send_report_to_invoked = kSendReportToInvokedTrue,
      .register_ad_beacon_invoked = kRegisterAdBeaconInvokedTrue};
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
  absl::StatusOr<ReportWinResponse> response =
      ParseReportWinResponse(config, json_string.value(), {}, log_context);
  TestResponse(response.value(), expected_response);
}

TEST(ParseReportResultResponse, ParsesReportWinResponseSuccessfullyWithPAgg) {
  server_common::log::SetGlobalPSVLogLevel(10);
  ReportWinResponse expected_response{
      .report_win_url = kTestReportResultUrl,
      .send_report_to_invoked = kSendReportToInvokedTrue,
      .register_ad_beacon_invoked = kRegisterAdBeaconInvokedTrue};
  expected_response.interaction_reporting_urls.try_emplace(
      kTestInteractionEvent, kTestInteractionUrl);
  ReportingDispatchRequestConfig config = {.enable_adtech_code_logging = true};
  ReportingResponseLogs console_logs;
  console_logs.logs.emplace_back(kTestLog);
  console_logs.warnings.emplace_back(kTestLog);
  console_logs.errors.emplace_back(kTestLog);
  PrivateAggregateContribution win_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  PrivateAggregateContribution loss_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "");
  PrivateAggregateContribution always_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_ALWAYS, "");
  PrivateAggregateContribution custom_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_CUSTOM, kTestCustomEvent);
  *expected_response.pagg_response.add_contributions() = win_contribution;
  *expected_response.pagg_response.add_contributions() = loss_contribution;
  *expected_response.pagg_response.add_contributions() = custom_contribution;
  *expected_response.pagg_response.add_contributions() = always_contribution;
  absl::StatusOr<std::string> json_string =
      BuildJsonObjectWithPrivateAggregateContributions(expected_response,
                                                       console_logs, config);
  ASSERT_TRUE(json_string.ok()) << json_string.status();
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  // Event type is not expected to be set.
  for (auto& contribution :
       *expected_response.pagg_response.mutable_contributions()) {
    if (contribution.event().event_type() != EVENT_TYPE_CUSTOM) {
      contribution.clear_event();
    }
  }
  // Loss contributions are expected to be filtered out.
  expected_response.pagg_response.mutable_contributions()->RemoveLast();

  absl::StatusOr<ReportWinResponse> response =
      ParseReportWinResponse(config, json_string.value(), {}, log_context);
  TestResponse(response.value(), expected_response);
}

TEST(GetBuyerDeviceSignals,
     ReturnsBuyerReportingSignalsWithSelectedBuyerAndSellerReportingId) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData reporting_dispatch_data =
      GetTestBuyerReportingDispatchRequestData(log_context);
  reporting_dispatch_data.interest_group_name = "";
  reporting_dispatch_data.buyer_and_seller_reporting_id =
      kTestBuyerAndSellerReportingId;
  reporting_dispatch_data.selected_buyer_and_seller_reporting_id =
      kTestSelectedBuyerAndSellerReportingId;
  rapidjson::Document document(rapidjson::kObjectType);
  absl::StatusOr<std::shared_ptr<std::string>> buyer_device_signals =
      GenerateBuyerDeviceSignals(reporting_dispatch_data, document);
  ASSERT_TRUE(buyer_device_signals.ok());
  VerifyPABuyerReportingSignalsJson(*buyer_device_signals,
                                    reporting_dispatch_data);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
