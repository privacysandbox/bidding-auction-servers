// Copyright 2023 Google LLC
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
#include "services/auction_service/reporting/reporting_helper.h"

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "api/bidding_auction_servers.pb.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/reporting/reporting_helper_test_constants.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/util/json_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

rapidjson::Document GetReportWinJsonObj(const ReportingResponse& response) {
  rapidjson::Document document;
  document.SetObject();
  // Convert the std::string to a rapidjson::Value object.
  rapidjson::Value report_win_url;
  report_win_url.SetString(response.report_win_response.report_win_url.c_str(),
                           document.GetAllocator());

  document.AddMember(kReportWinUrl, report_win_url, document.GetAllocator());
  document.AddMember(kSendReportToInvoked,
                     response.report_win_response.send_report_to_invoked,
                     document.GetAllocator());
  document.AddMember(kRegisterAdBeaconInvoked,
                     response.report_win_response.register_ad_beacon_invoked,
                     document.GetAllocator());
  rapidjson::Value map_value(rapidjson::kObjectType);
  for (const auto& [event, url] :
       response.report_win_response.interaction_reporting_urls) {
    rapidjson::Value interaction_event;
    interaction_event.SetString(event.c_str(), document.GetAllocator());
    rapidjson::Value interaction_url;
    interaction_url.SetString(url.c_str(), document.GetAllocator());
    map_value.AddMember(interaction_event, interaction_url,
                        document.GetAllocator());
  }
  document.AddMember(kInteractionReportingUrlsWrapperResponse, map_value,
                     document.GetAllocator());
  return document;
}

rapidjson::Document GetReportResultJsonObj(const ReportingResponse& response) {
  rapidjson::Document document;
  document.SetObject();
  // Convert the std::string to a rapidjson::Value object.
  rapidjson::Value report_result_url;
  report_result_url.SetString(
      response.report_result_response.report_result_url.c_str(),
      document.GetAllocator());

  rapidjson::Value seller_logs(rapidjson::kArrayType);
  for (const std::string& log : response.seller_logs) {
    rapidjson::Value value(log.c_str(), log.size(), document.GetAllocator());
    seller_logs.PushBack(value, document.GetAllocator());
  }
  document.AddMember(kReportResultUrl, report_result_url,
                     document.GetAllocator());
  document.AddMember(kSendReportToInvoked,
                     response.report_result_response.send_report_to_invoked,
                     document.GetAllocator());
  document.AddMember(kRegisterAdBeaconInvoked,
                     response.report_result_response.register_ad_beacon_invoked,
                     document.GetAllocator());
  rapidjson::Value map_value(rapidjson::kObjectType);
  for (const auto& [event, url] :
       response.report_result_response.interaction_reporting_urls) {
    rapidjson::Value interaction_event;
    interaction_event.SetString(event.c_str(), document.GetAllocator());
    rapidjson::Value interaction_url;
    interaction_url.SetString(url.c_str(), document.GetAllocator());
    map_value.AddMember(interaction_event, interaction_url,
                        document.GetAllocator());
  }
  document.AddMember(kInteractionReportingUrlsWrapperResponse, map_value,
                     document.GetAllocator());
  return document;
}

absl::StatusOr<std::string> BuildJsonObject(const ReportingResponse& response,
                                            bool set_logs = true) {
  rapidjson::Document outerDoc;
  outerDoc.SetObject();
  rapidjson::Document report_result_obj = GetReportResultJsonObj(response);
  rapidjson::Document report_win_obj = GetReportWinJsonObj(response);
  if (set_logs) {
    rapidjson::Value seller_logs(rapidjson::kArrayType);
    for (const std::string& log : response.seller_logs) {
      rapidjson::Value value(log.c_str(), log.size(), outerDoc.GetAllocator());
      seller_logs.PushBack(value, outerDoc.GetAllocator());
    }
    rapidjson::Value seller_warning_logs(rapidjson::kArrayType);
    for (const std::string& seller_warning : response.seller_warning_logs) {
      rapidjson::Value value(seller_warning.c_str(), seller_warning.size(),
                             outerDoc.GetAllocator());
      seller_warning_logs.PushBack(value, outerDoc.GetAllocator());
    }
    rapidjson::Value seller_error_logs(rapidjson::kArrayType);
    for (const std::string& seller_error : response.seller_error_logs) {
      rapidjson::Value value(seller_error.c_str(), seller_error.size(),
                             outerDoc.GetAllocator());
      seller_error_logs.PushBack(value, outerDoc.GetAllocator());
    }
    rapidjson::Value buyer_logs(rapidjson::kArrayType);
    for (const std::string& buyer_log : response.buyer_logs) {
      rapidjson::Value value(buyer_log.c_str(), buyer_log.size(),
                             outerDoc.GetAllocator());
      buyer_logs.PushBack(value, outerDoc.GetAllocator());
    }
    rapidjson::Value buyer_error_logs(rapidjson::kArrayType);
    for (const std::string& buyer_error : response.buyer_error_logs) {
      rapidjson::Value value(buyer_error.c_str(), buyer_error.size(),
                             outerDoc.GetAllocator());
      buyer_error_logs.PushBack(value, outerDoc.GetAllocator());
    }
    rapidjson::Value buyer_warning_logs(rapidjson::kArrayType);
    for (const std::string& buyer_warning : response.buyer_warning_logs) {
      rapidjson::Value value(buyer_warning.c_str(), buyer_warning.size(),
                             outerDoc.GetAllocator());
      buyer_warning_logs.PushBack(value, outerDoc.GetAllocator());
    }
    outerDoc.AddMember(kSellerLogs, seller_logs, outerDoc.GetAllocator());
    outerDoc.AddMember(kSellerErrors, seller_error_logs,
                       outerDoc.GetAllocator());
    outerDoc.AddMember(kSellerWarnings, seller_warning_logs,
                       outerDoc.GetAllocator());
    outerDoc.AddMember(kBuyerLogs, buyer_logs, outerDoc.GetAllocator());
    outerDoc.AddMember(kBuyerErrors, buyer_error_logs, outerDoc.GetAllocator());
    outerDoc.AddMember(kBuyerWarnings, buyer_warning_logs,
                       outerDoc.GetAllocator());
    outerDoc.AddMember(kBuyerLogs, buyer_logs, outerDoc.GetAllocator());
  }
  outerDoc.AddMember(kReportResultResponse, report_result_obj,
                     outerDoc.GetAllocator());
  outerDoc.AddMember(kReportWinResponse, report_win_obj,
                     outerDoc.GetAllocator());
  return SerializeJsonDoc(outerDoc);
}

void TestResponse(const ReportingResponse& response,
                  const ReportingResponse& expected_response) {
  EXPECT_EQ(response.report_result_response.report_result_url,
            expected_response.report_result_response.report_result_url);
  EXPECT_EQ(response.report_result_response.send_report_to_invoked,
            expected_response.report_result_response.send_report_to_invoked);
  EXPECT_EQ(
      response.report_result_response.register_ad_beacon_invoked,
      expected_response.report_result_response.register_ad_beacon_invoked);
  EXPECT_EQ(response.report_result_response.interaction_reporting_urls.size(),
            expected_response.report_result_response.interaction_reporting_urls
                .size());
  EXPECT_EQ(response.seller_logs.size(), expected_response.seller_logs.size());
  EXPECT_EQ(response.seller_error_logs.size(),
            expected_response.seller_error_logs.size());
  EXPECT_EQ(response.seller_warning_logs.size(),
            expected_response.seller_warning_logs.size());
  EXPECT_EQ(response.buyer_logs.size(), expected_response.buyer_logs.size());
  EXPECT_EQ(response.buyer_error_logs.size(),
            expected_response.buyer_error_logs.size());
  EXPECT_EQ(response.buyer_warning_logs.size(),
            expected_response.buyer_warning_logs.size());
}

void TestArgs(std::vector<std::shared_ptr<std::string>> response_vector,
              absl::string_view enable_adtech_code_logging,
              const BuyerReportingMetadata& expected_buyer_metadata = {}) {
  EXPECT_EQ(
      *(response_vector[ReportingArgIndex(ReportingArgs::kAuctionConfig)]),
      kTestAuctionConfig);
  EXPECT_EQ(*(response_vector[ReportingArgIndex(
                ReportingArgs::kSellerReportingSignals)]),
            kTestSellerReportingSignals);
  EXPECT_EQ(*(response_vector[ReportingArgIndex(
                ReportingArgs::kDirectFromSellerSignals)]),
            "{}");
  EXPECT_EQ(*(response_vector[ReportingArgIndex(
                ReportingArgs::kEnableAdTechCodeLogging)]),
            enable_adtech_code_logging);
  if (!expected_buyer_metadata.enable_report_win_url_generation) {
    EXPECT_EQ(*(response_vector[ReportingArgIndex(
                  ReportingArgs::kBuyerReportingMetadata)]),
              kDefaultBuyerReportingMetadata);
  } else {
    EXPECT_EQ(*(response_vector[ReportingArgIndex(
                  ReportingArgs::kBuyerReportingMetadata)]),
              kTestBuyerMetadata);
  }
}

TEST(ParseAndGetReportingResponseJson, ParsesReportingResultSuccessfully) {
  bool enable_adtech_code_logging = true;
  ReportingResponse expected_response = {
      .report_result_response = {.report_result_url = kTestReportResultUrl,
                                 .send_report_to_invoked =
                                     kSendReportToInvokedTrue,
                                 .register_ad_beacon_invoked =
                                     kRegisterAdBeaconInvokedTrue},
      .report_win_response = {
          .report_win_url = kTestReportWinUrl,
          .send_report_to_invoked = kSendReportToInvokedTrue,
          .register_ad_beacon_invoked = kRegisterAdBeaconInvokedTrue}};
  expected_response.seller_logs.emplace_back(kTestLog);
  expected_response.seller_warning_logs.emplace_back(kTestLog);
  expected_response.seller_error_logs.emplace_back(kTestLog);
  expected_response.buyer_logs.emplace_back(kTestLog);
  expected_response.buyer_error_logs.emplace_back(kTestLog);
  expected_response.buyer_warning_logs.emplace_back(kTestLog);
  expected_response.report_result_response.interaction_reporting_urls
      .try_emplace(kTestInteractionEvent, kTestInteractionUrl);
  expected_response.report_win_response.interaction_reporting_urls.try_emplace(
      kTestInteractionEvent, kTestInteractionUrl);
  absl::StatusOr<std::string> json_string = BuildJsonObject(expected_response);
  ASSERT_TRUE(json_string.ok()) << json_string.status();
  absl::StatusOr<ReportingResponse> response = ParseAndGetReportingResponse(
      enable_adtech_code_logging, json_string.value());
  TestResponse(response.value(), expected_response);
}

TEST(ParseAndGetReportingResponseJson, OnlyFewFieldsSetParsesSuccessfully) {
  bool enable_adtech_code_logging = true;
  bool set_logs = false;
  ReportingResponse expected_response;
  expected_response.report_result_response.report_result_url = kReportResultUrl;
  expected_response.report_result_response.send_report_to_invoked =
      kSendReportToInvokedTrue;
  expected_response.report_result_response.register_ad_beacon_invoked =
      kRegisterAdBeaconInvokedTrue;
  absl::StatusOr<std::string> json_string =
      BuildJsonObject(expected_response, set_logs);
  ASSERT_TRUE(json_string.ok()) << json_string.status();
  absl::StatusOr<ReportingResponse> response = ParseAndGetReportingResponse(
      enable_adtech_code_logging, json_string.value());
  TestResponse(response.value(), expected_response);
}

TEST(ParseAndGetReportingResponseJson, HandlesEmptyResponse) {
  bool enable_adtech_code_logging = true;
  ReportingResponse expected_response;
  expected_response.report_result_response.send_report_to_invoked = false;
  expected_response.report_result_response.register_ad_beacon_invoked = false;
  absl::StatusOr<std::string> json_string = BuildJsonObject(expected_response);
  ASSERT_TRUE(json_string.ok()) << json_string.status();
  absl::StatusOr<ReportingResponse> response =
      ParseAndGetReportingResponse(enable_adtech_code_logging, "{}");
  TestResponse(response.value(), expected_response);
}

TEST(GetReportingInput, ReturnsTheInputArgsForReportResult) {
  ScoreAdsResponse::AdScore winning_ad_score;
  winning_ad_score.set_buyer_bid(kTestBuyerBid);
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
  bool enable_adtech_code_logging = true;
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  const ContextLogger logger{};
  std::vector<std::shared_ptr<std::string>> response_vector =
      GetReportingInput(winning_ad_score, kTestPublisherHostName,
                        enable_adtech_code_logging, auction_config, logger, {});
  TestArgs(response_vector, kEnableAdtechCodeLoggingTrue);
}

TEST(GetReportingDispatchRequest, ReturnsTheDispatchRequestForReportResult) {
  ScoreAdsResponse::AdScore winning_ad_score;
  winning_ad_score.set_buyer_bid(kTestBuyerBid);
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
  bool enable_adtech_code_logging = true;
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  const ContextLogger logger{};
  DispatchRequest request = GetReportingDispatchRequest(
      winning_ad_score, kTestPublisherHostName, enable_adtech_code_logging,
      auction_config, logger, {});
  TestArgs(request.input, kEnableAdtechCodeLoggingTrue);
  EXPECT_EQ(request.id, kTestRender);
  EXPECT_EQ(request.handler_name, kReportingDispatchHandlerFunctionName);
  EXPECT_EQ(request.version_num, kDispatchRequestVersionNumber);
}

TEST(GetReportingDispatchRequest, ReturnsDispatchRequestWithReportWin) {
  ScoreAdsResponse::AdScore winning_ad_score;
  winning_ad_score.set_buyer_bid(kTestBuyerBid);
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
  bool enable_adtech_code_logging = true;
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  const ContextLogger logger{};
  const BuyerReportingMetadata buyer_reporting_metadata = {
      .enable_report_win_url_generation = true,
      .buyer_signals = kTestBuyerSignals,
      .join_count = kTestJoinCount,
      .recency = kTestRecency,
      .modeling_signals = kTestModelingSignals,
      .seller = kTestSeller,
      .interest_group_name = kTestInterestGroupName,
      .ad_cost = kTestAdCost};
  DispatchRequest request = GetReportingDispatchRequest(
      winning_ad_score, kTestPublisherHostName, enable_adtech_code_logging,
      auction_config, logger, buyer_reporting_metadata);
  TestArgs(request.input, kEnableAdtechCodeLoggingTrue,
           buyer_reporting_metadata);
  EXPECT_EQ(request.id, kTestRender);
  EXPECT_EQ(request.handler_name, kReportingDispatchHandlerFunctionName);
  EXPECT_EQ(request.version_num, kDispatchRequestVersionNumber);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
