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
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "api/bidding_auction_servers.pb.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/constants/common_constants.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr char kTestEgressPayload[] = "testEgressPayload";
constexpr int kMinNoisedJoinCount = 0;
constexpr int kMaxNoisedJoinCount = 17;
constexpr int kMinNoisedRecency = -1;
constexpr int kMaxNoisedRecency = 33;
constexpr int kMinNoisedModelingSignals = -1;
constexpr int kMaxNoisedModelingSignals = 65536;
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

BuyerReportingMetadata GetTestBuyerReportingMetadata() {
  return {
      .buyer_signals = kTestBuyerSignalsObj,
      .join_count = kTestJoinCount,
      .recency = kTestRecency,
      .modeling_signals = kTestModelingSignals,
      .seller = kTestSeller.data(),
      .interest_group_name = kTestInterestGroupName.data(),
      .ad_cost = kTestAdCost,
      .data_version = kTestDataVersion,
  };
}

ReportingDispatchRequestData GetTestDispatchRequestData(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const ReportingDispatchRequestConfig& dispatch_request_config,
    // NOLINTNEXTLINE
    const std::string& handler_name, std::string seller_currency,
    const uint32_t seller_data_version) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  ReportingDispatchRequestData reporting_dispatch_request_data = {
      .handler_name = handler_name,
      .auction_config = auction_config,
      .post_auction_signals = GeneratePostAuctionSignals(
          winning_ad_score, std::move(seller_currency), seller_data_version),
      .publisher_hostname = kTestPublisherHostName,
      .log_context = log_context};
  if (dispatch_request_config.enable_report_win_url_generation) {
    reporting_dispatch_request_data.buyer_reporting_metadata =
        GetTestBuyerReportingMetadata();
  }
  return reporting_dispatch_request_data;
}

ReportingDispatchRequestData GetTestComponentDispatchRequestData(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const ReportingDispatchRequestConfig& dispatch_request_config,
    // NOLINTNEXTLINE
    const std::string& handler_name, std::string seller_currency,
    const uint32_t seller_data_version) {
  ReportingDispatchRequestData reporting_dispatch_request_data =
      GetTestDispatchRequestData(winning_ad_score, dispatch_request_config,
                                 handler_name, std::move(seller_currency),
                                 seller_data_version);
  reporting_dispatch_request_data.component_reporting_metadata = {
      .top_level_seller = kTestTopLevelSeller,
      .component_seller = kTestSeller.data(),
      .modified_bid_currency = winning_ad_score.bid_currency(),
      .modified_bid = winning_ad_score.bid()};
  return reporting_dispatch_request_data;
}

ReportingDispatchRequestData GetTestComponentDispatchRequestDataForPAS(
    const ScoreAdsResponse::AdScore& winning_ad_score,
    const ReportingDispatchRequestConfig& dispatch_request_config,
    // NOLINTNEXTLINE
    std::string seller_currency, const uint32_t seller_data_version) {
  ReportingDispatchRequestData reporting_dispatch_request_data =
      GetTestDispatchRequestData(winning_ad_score, dispatch_request_config,
                                 kReportingProtectedAppSignalsFunctionName,
                                 std::move(seller_currency),
                                 seller_data_version);
  reporting_dispatch_request_data.egress_payload = kTestEgressPayload;
  return reporting_dispatch_request_data;
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
              const ReportingDispatchRequestConfig& dispatch_request_config,
              absl::string_view expected_seller_reporting_signals,
              absl::string_view expected_unnoised_buyer_metadata_json =
                  kTestBuyerMetadata,
              absl::string_view expected_egress_payload = "") {
  EXPECT_EQ(
      *(response_vector[ReportingArgIndex(ReportingArgs::kAuctionConfig)]),
      kTestAuctionConfig);
  EXPECT_EQ(*(response_vector[ReportingArgIndex(
                ReportingArgs::kSellerReportingSignals)]),
            expected_seller_reporting_signals);
  EXPECT_EQ(*(response_vector[ReportingArgIndex(
                ReportingArgs::kDirectFromSellerSignals)]),
            "{}");
  EXPECT_EQ(
      *(response_vector[ReportingArgIndex(
          ReportingArgs::kEnableAdTechCodeLogging)]),
      dispatch_request_config.enable_adtech_code_logging ? "true" : "false");
  if (!dispatch_request_config.enable_report_win_url_generation) {
    EXPECT_EQ(*(response_vector[ReportingArgIndex(
                  ReportingArgs::kBuyerReportingMetadata)]),
              kDefaultBuyerReportingMetadata);
  } else if (!expected_unnoised_buyer_metadata_json.empty()) {
    EXPECT_EQ(*(response_vector[ReportingArgIndex(
                  ReportingArgs::kBuyerReportingMetadata)]),
              expected_unnoised_buyer_metadata_json);
  }
  if (dispatch_request_config.enable_protected_app_signals) {
    EXPECT_EQ(
        *(response_vector[ReportingArgIndex(ReportingArgs::kEgressPayload)]),
        absl::StrCat("\"", expected_egress_payload, "\""));
  }
}

absl::StatusOr<BuyerReportingMetadata> ParseBuyerReportingMetadata(
    const std::string& buyer_reporting_metadata_json) {
  BuyerReportingMetadata reporting_metadata{};
  PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                      ParseJsonString(buyer_reporting_metadata_json));
  rapidjson::Value buyer_signals;
  PS_ASSIGN_IF_PRESENT(buyer_signals, document, kBuyerSignals, Object);
  PS_ASSIGN_OR_RETURN(reporting_metadata.buyer_signals,
                      SerializeJsonDoc(buyer_signals));
  PS_ASSIGN_IF_PRESENT(reporting_metadata.seller, document, kSellerTag, String);
  PS_ASSIGN_IF_PRESENT(reporting_metadata.interest_group_name, document,
                       kInterestGroupName, String);
  PS_ASSIGN_IF_PRESENT(reporting_metadata.ad_cost, document, kAdCostTag, Float);
  PS_ASSIGN_IF_PRESENT(reporting_metadata.join_count, document, kJoinCount,
                       Int);
  PS_ASSIGN_IF_PRESENT(reporting_metadata.recency, document, kRecency, Int64);
  PS_ASSIGN_IF_PRESENT(reporting_metadata.modeling_signals, document,
                       kModelingSignalsTag, Int);
  PS_ASSIGN_IF_PRESENT(reporting_metadata.data_version, document, kDataVersion,
                       Uint);
  return reporting_metadata;
}

void VerifyBuyerReportingMetadata(
    const std::string& buyer_reporting_metadata_json,
    const BuyerReportingMetadata& expected_buyer_reporting_metadata) {
  absl::StatusOr<BuyerReportingMetadata> parsed_buyer_reporting_metadata =
      ParseBuyerReportingMetadata(buyer_reporting_metadata_json);
  ASSERT_TRUE(parsed_buyer_reporting_metadata.ok())
      << parsed_buyer_reporting_metadata.status();
  BuyerReportingMetadata buyer_reporting_metadata =
      *std::move(parsed_buyer_reporting_metadata);
  EXPECT_EQ(buyer_reporting_metadata.buyer_signals,
            expected_buyer_reporting_metadata.buyer_signals);
  EXPECT_EQ(buyer_reporting_metadata.interest_group_name,
            expected_buyer_reporting_metadata.interest_group_name);
  EXPECT_EQ(buyer_reporting_metadata.seller,
            expected_buyer_reporting_metadata.seller);
  EXPECT_EQ(buyer_reporting_metadata.ad_cost,
            expected_buyer_reporting_metadata.ad_cost);
  EXPECT_EQ(buyer_reporting_metadata.data_version,
            expected_buyer_reporting_metadata.data_version);
  if (expected_buyer_reporting_metadata.join_count.has_value() &&
      buyer_reporting_metadata.join_count.has_value()) {
    if (buyer_reporting_metadata.join_count.value() !=
        expected_buyer_reporting_metadata.join_count.value()) {
      EXPECT_GT(buyer_reporting_metadata.join_count.value(),
                kMinNoisedJoinCount);
      EXPECT_LT(buyer_reporting_metadata.join_count.value(),
                kMaxNoisedJoinCount);
    } else {
      EXPECT_EQ(buyer_reporting_metadata.join_count.value(),
                expected_buyer_reporting_metadata.join_count.value());
    }
  }
  if (expected_buyer_reporting_metadata.recency.has_value() &&
      buyer_reporting_metadata.recency.has_value()) {
    if (buyer_reporting_metadata.recency.value() !=
        expected_buyer_reporting_metadata.recency.value()) {
      EXPECT_GT(buyer_reporting_metadata.recency.value(), kMinNoisedRecency);
      EXPECT_LT(buyer_reporting_metadata.recency.value(), kMaxNoisedRecency);
    } else {
      EXPECT_EQ(buyer_reporting_metadata.recency.value(),
                expected_buyer_reporting_metadata.recency.value());
    }
  }
  if (expected_buyer_reporting_metadata.modeling_signals.has_value() &&
      buyer_reporting_metadata.modeling_signals.has_value()) {
    if (buyer_reporting_metadata.modeling_signals.value() !=
        expected_buyer_reporting_metadata.modeling_signals.value()) {
      EXPECT_GT(buyer_reporting_metadata.modeling_signals.value(),
                kMinNoisedModelingSignals);
      EXPECT_LT(buyer_reporting_metadata.modeling_signals.value(),
                kMaxNoisedModelingSignals);
    } else {
      EXPECT_EQ(buyer_reporting_metadata.modeling_signals.value(),
                expected_buyer_reporting_metadata.modeling_signals.value());
    }
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

TEST(GetReportingInput, ReturnsTheInputArgsForReportResultForComponentAuction) {
  ScoreAdsResponse::AdScore winning_ad_score;
  winning_ad_score.set_buyer_bid(kTestBuyerBid);
  winning_ad_score.set_buyer_bid_currency(kEurosIsoCode);
  winning_ad_score.set_bid(kTestModifiedBid);
  // This is the currency of the modified bid.
  winning_ad_score.set_bid_currency(kUsdIsoCode);
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
  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_adtech_code_logging = true};
  ReportingDispatchRequestData dispatch_request_data =
      GetTestComponentDispatchRequestData(
          winning_ad_score, dispatch_request_config,
          kReportingDispatchHandlerFunctionName, /*seller_currency=*/"",
          kSellerDataVersion);
  std::vector<std::shared_ptr<std::string>> response_vector =
      GetReportingInput(dispatch_request_config, dispatch_request_data);
  TestArgs(response_vector, dispatch_request_config,
           kTestSellerReportingSignalsForComponentSeller);
}

TEST(GetReportingDispatchRequest, ReturnsTheDispatchRequestForReportResult) {
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
  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_adtech_code_logging = true};
  ReportingDispatchRequestData dispatch_request_data =
      GetTestDispatchRequestData(winning_ad_score, dispatch_request_config,
                                 kReportingDispatchHandlerFunctionName,
                                 /*seller_currency=*/kUsdIsoCode,
                                 kSellerDataVersion);
  DispatchRequest request = GetReportingDispatchRequest(dispatch_request_config,
                                                        dispatch_request_data);
  TestArgs(request.input, dispatch_request_config,
           kTestSellerReportingSignalsWithOtherBidCurrency);
  EXPECT_EQ(request.id, kTestRender);
  EXPECT_EQ(request.handler_name, kReportingDispatchHandlerFunctionName);
  EXPECT_EQ(request.version_string, kReportingBlobVersion);
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
  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_report_win_url_generation = true,
      .enable_report_win_input_noising = false,
      .enable_adtech_code_logging = true};
  ReportingDispatchRequestData dispatch_request_data =
      GetTestDispatchRequestData(winning_ad_score, dispatch_request_config,
                                 kReportingDispatchHandlerFunctionName,
                                 /*seller_currency=*/"", kSellerDataVersion);
  DispatchRequest request = GetReportingDispatchRequest(dispatch_request_config,
                                                        dispatch_request_data);
  TestArgs(request.input, dispatch_request_config, kTestSellerReportingSignals);
  EXPECT_EQ(request.id, kTestRender);
  EXPECT_EQ(request.handler_name, kReportingDispatchHandlerFunctionName);
  EXPECT_EQ(request.version_string, kReportingBlobVersion);
}

TEST(GetReportingDispatchRequest,
     DispatchRequestSuccessWithReportWinAndNoisingEnabled) {
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
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_report_win_url_generation = true,
      .enable_report_win_input_noising = true,
      .enable_adtech_code_logging = true};
  ReportingDispatchRequestData reporting_dispatch_request_data =
      GetTestDispatchRequestData(winning_ad_score, dispatch_request_config,
                                 kReportingDispatchHandlerFunctionName,
                                 /*seller_currency=*/"", kSellerDataVersion);
  DispatchRequest request = GetReportingDispatchRequest(
      dispatch_request_config, reporting_dispatch_request_data);
  TestArgs(request.input, dispatch_request_config, kTestSellerReportingSignals,
           /*expected_unnoised_buyer_metadata_json=*/"");
  VerifyBuyerReportingMetadata(
      *(request
            .input[ReportingArgIndex(ReportingArgs::kBuyerReportingMetadata)]),
      GetTestBuyerReportingMetadata());
  EXPECT_EQ(request.id, kTestRender);
  EXPECT_EQ(request.handler_name, kReportingDispatchHandlerFunctionName);
  EXPECT_EQ(request.version_string, kReportingBlobVersion);
}

TEST(GetReportingDispatchRequest,
     ReturnsDispatchRequestWithReportWinForProtectedAppSignals) {
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
  ReportingDispatchRequestConfig dispatch_request_config = {
      .enable_report_win_url_generation = true,
      .enable_protected_app_signals = true,
      .enable_report_win_input_noising = true};
  ReportingDispatchRequestData dispatch_request_data =
      GetTestComponentDispatchRequestDataForPAS(
          winning_ad_score, dispatch_request_config, /*seller_currency=*/"",
          kSellerDataVersion);
  DispatchRequest request = GetReportingDispatchRequest(dispatch_request_config,
                                                        dispatch_request_data);
  TestArgs(request.input, dispatch_request_config, kTestSellerReportingSignals,
           /*expected_unnoised_buyer_metadata_json=*/"",
           dispatch_request_data.egress_payload);
  VerifyBuyerReportingMetadata(
      *(request
            .input[ReportingArgIndex(ReportingArgs::kBuyerReportingMetadata)]),
      GetTestBuyerReportingMetadata());
  EXPECT_EQ(request.id, kTestRender);
  EXPECT_EQ(request.handler_name, kReportingProtectedAppSignalsFunctionName);
  EXPECT_EQ(request.version_string, kReportingBlobVersion);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
