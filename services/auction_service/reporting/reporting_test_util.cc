/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/auction_service/reporting/reporting_test_util.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_helper_test_constants.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

// Upper and lower bounds of noised buyerDeviceSignals based on
// https://github.com/WICG/turtledove/blob/main/FLEDGE.md#521-noised-and-bucketed-signals
// jointCount is expected to be between 0-17(4 bits)
constexpr int kJoinCountLowerBound = 0;
constexpr int kJoinCountUpperBound = 17;
// Recency is expected to be between 0-32 (5-bits)
constexpr int kRecencyLowerBound = 0;
constexpr int kRecencyUpperBound = 32;
// Modeling signals is expected to be between 0-65536(16 bits)
constexpr int kModelingSignalsLowerBound = 0;
constexpr int kModelingSignalsUpperBound = 65536;

inline void SetAdTechLogs(const std::vector<std::string>& logs,
                          const rapidjson::GenericStringRef<char>& tag,
                          rapidjson::Document& doc) {
  rapidjson::Value log_value(rapidjson::kArrayType);
  for (const std::string& log : logs) {
    rapidjson::Value value(log.c_str(), log.size(), doc.GetAllocator());
    log_value.PushBack(value, doc.GetAllocator());
  }
  doc.AddMember(tag, log_value, doc.GetAllocator());
}
}  // namespace
void SetAdTechLogs(const ReportingResponseLogs& console_logs,
                   rapidjson::Document& doc) {
  SetAdTechLogs(console_logs.logs, kReportingUdfLogs, doc);
  SetAdTechLogs(console_logs.warnings, kReportingUdfWarnings, doc);
  SetAdTechLogs(console_logs.errors, kReportingUdfErrors, doc);
}
TestSellerDeviceSignals ParseSellerDeviceSignals(
    rapidjson::Document& document) {
  TestSellerDeviceSignals seller_device_signals;
  PS_ASSIGN_IF_PRESENT(seller_device_signals.top_window_hostname, document,
                       kTopWindowHostnameTag, String);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.render_url, document,
                       kRenderUrlTag, String);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.render_URL, document,
                       kRenderURLTag, String);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.bid, document, kBidTag, Float);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.bid_currency, document,
                       kWinningBidCurrencyTag, String);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.highest_scoring_other_bid_currency,
                       document, kHighestScoringOtherBidCurrencyTag, String);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.desirability, document,
                       kDesirabilityTag, Float);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.highest_scoring_other_bid,
                       document, kHighestScoringOtherBidTag, Float);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.modified_bid, document,
                       kModifiedBid, Float);
  PS_ASSIGN_IF_PRESENT(seller_device_signals.top_level_seller, document,
                       kTopLevelSellerTag, String);
  return seller_device_signals;
}

SellerReportingDispatchRequestData GetTestSellerDispatchRequestData(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context) {
  auto auction_config = std::make_shared<std::string>(kTestAuctionConfig);
  SellerReportingDispatchRequestData reporting_dispatch_request_data = {
      .auction_config = auction_config,
      .post_auction_signals = post_auction_signals,
      .publisher_hostname = kTestPublisherHostName,
      .component_reporting_metadata = {},
      .log_context = log_context};
  return reporting_dispatch_request_data;
}

SellerReportingDispatchRequestData
GetTestSellerDispatchRequestDataForComponentAuction(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context) {
  auto auction_config = std::make_shared<std::string>(kTestAuctionConfig);
  SellerReportingDispatchRequestData reporting_dispatch_request_data = {
      .auction_config = auction_config,
      .post_auction_signals = post_auction_signals,
      .publisher_hostname = kTestPublisherHostName,
      .component_reporting_metadata = {.top_level_seller = kTestTopLevelSeller,
                                       .modified_bid = kTestModifiedBid},
      .log_context = log_context};
  return reporting_dispatch_request_data;
}

SellerReportingDispatchRequestData
GetTestSellerDispatchRequestDataForTopLevelAuction(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context) {
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  SellerReportingDispatchRequestData reporting_dispatch_request_data = {
      .auction_config = auction_config,
      .post_auction_signals = post_auction_signals,
      .publisher_hostname = kTestPublisherHostName,
      .component_reporting_metadata = {.component_seller = kTestTopLevelSeller},
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

rapidjson::Document GenerateTestSellerDeviceSignals(
    const SellerReportingDispatchRequestData& dispatch_request_data) {
  rapidjson::Document json_doc =
      GenerateSellerDeviceSignals(dispatch_request_data);
  return json_doc;
}

void VerifyBuyerReportingSignals(
    BuyerReportingDispatchRequestData& observed_buyer_device_signals,
    const BuyerReportingDispatchRequestData& expected_buyer_device_signals) {
  if (expected_buyer_device_signals.buyer_reporting_id.has_value()) {
    EXPECT_EQ(*observed_buyer_device_signals.buyer_reporting_id,
              *expected_buyer_device_signals.buyer_reporting_id);
  } else {
    EXPECT_EQ(observed_buyer_device_signals.interest_group_name,
              expected_buyer_device_signals.interest_group_name);
  }
  EXPECT_EQ(observed_buyer_device_signals.seller,
            expected_buyer_device_signals.seller);
  EXPECT_EQ(observed_buyer_device_signals.ad_cost,
            expected_buyer_device_signals.ad_cost);
  if (expected_buyer_device_signals.join_count.has_value() &&
      *observed_buyer_device_signals.join_count !=
          *expected_buyer_device_signals.join_count) {
    EXPECT_GT(*observed_buyer_device_signals.join_count,
              kJoinCountLowerBound - 1);
    EXPECT_LT(*observed_buyer_device_signals.join_count,
              kJoinCountUpperBound + 1);
  } else {
    EXPECT_EQ(*observed_buyer_device_signals.join_count,
              *expected_buyer_device_signals.join_count);
  }
  if (expected_buyer_device_signals.recency.has_value() &&
      *observed_buyer_device_signals.recency !=
          *expected_buyer_device_signals.recency) {
    EXPECT_GT(*observed_buyer_device_signals.recency, kRecencyLowerBound - 1);
    EXPECT_LT(*observed_buyer_device_signals.recency, kRecencyUpperBound + 1);
  } else {
    EXPECT_EQ(*observed_buyer_device_signals.recency,
              *expected_buyer_device_signals.recency);
  }
  if (expected_buyer_device_signals.modeling_signals.has_value() &&
      *observed_buyer_device_signals.modeling_signals !=
          *expected_buyer_device_signals.join_count) {
    EXPECT_GT(*observed_buyer_device_signals.modeling_signals,
              kModelingSignalsLowerBound - 1);
    EXPECT_LT(*observed_buyer_device_signals.modeling_signals,
              kModelingSignalsUpperBound + 1);
  } else {
    EXPECT_EQ(*observed_buyer_device_signals.modeling_signals,
              *expected_buyer_device_signals.modeling_signals);
  }
  EXPECT_EQ(observed_buyer_device_signals.made_highest_scoring_other_bid,
            expected_buyer_device_signals.made_highest_scoring_other_bid);
  if (observed_buyer_device_signals.egress_payload.has_value()) {
    EXPECT_EQ(*observed_buyer_device_signals.egress_payload,
              *expected_buyer_device_signals.egress_payload);
  }
  if (observed_buyer_device_signals.temporary_unlimited_egress_payload
          .has_value()) {
    EXPECT_EQ(
        *observed_buyer_device_signals.temporary_unlimited_egress_payload,
        *expected_buyer_device_signals.temporary_unlimited_egress_payload);
  }
}

void ParseBuyerReportingSignals(
    BuyerReportingDispatchRequestData& reporting_dispatch_data,
    rapidjson::Document& document) {
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.seller, document, kSellerTag,
                       String);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.buyer_reporting_id, document,
                       kBuyerReportingIdTag, String);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.interest_group_name, document,
                       kInterestGroupNameTag, String);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.ad_cost, document, kAdCostTag,
                       Float);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.join_count, document,
                       kJoinCountTag, Int);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.recency, document, kRecencyTag,
                       Int64);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.modeling_signals, document,
                       kModelingSignalsTag, Int);
  PS_ASSIGN_IF_PRESENT(reporting_dispatch_data.made_highest_scoring_other_bid,
                       document, kMadeHighestScoringOtherBid, Bool);
  ASSERT_FALSE(document.HasMember(kModifiedBid));
}

void VerifySellerDeviceSignals(
    const TestSellerDeviceSignals& observed_seller_device_signals,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data) {
  EXPECT_EQ(observed_seller_device_signals.top_window_hostname,
            seller_dispatch_request_data.publisher_hostname);
  EXPECT_EQ(observed_seller_device_signals.highest_scoring_other_bid,
            seller_dispatch_request_data.post_auction_signals
                .highest_scoring_other_bid);
  EXPECT_EQ(observed_seller_device_signals.desirability, -1);
  EXPECT_EQ(
      observed_seller_device_signals.bid_currency,
      seller_dispatch_request_data.post_auction_signals.winning_bid_currency);
  EXPECT_EQ(observed_seller_device_signals.highest_scoring_other_bid_currency,
            seller_dispatch_request_data.post_auction_signals
                .highest_scoring_other_bid_currency);
  EXPECT_EQ(observed_seller_device_signals.bid,
            seller_dispatch_request_data.post_auction_signals.winning_bid);
  EXPECT_EQ(
      observed_seller_device_signals.render_URL,
      seller_dispatch_request_data.post_auction_signals.winning_ad_render_url);
  EXPECT_EQ(
      observed_seller_device_signals.render_url,
      seller_dispatch_request_data.post_auction_signals.winning_ad_render_url);
}

void VerifyBuyerDeviceSignalsForComponentAuction(
    const TestSellerDeviceSignals&
        observed_seller_device_signals_in_buyer_device_signals,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data) {
  // modified_bid is not expected to set in the seller device signals for
  // component buyer
  EXPECT_FALSE(observed_seller_device_signals_in_buyer_device_signals
                   .modified_bid.has_value());
  EXPECT_EQ(
      *observed_seller_device_signals_in_buyer_device_signals.top_level_seller,
      seller_dispatch_request_data.component_reporting_metadata
          .top_level_seller);
  VerifySellerDeviceSignals(
      observed_seller_device_signals_in_buyer_device_signals,
      seller_dispatch_request_data);
}

void VerifyPABuyerReportingSignalsJson(
    const std::string& buyer_reporting_signals_json,
    const BuyerReportingDispatchRequestData&
        expected_buyer_dispatch_request_data,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData observed_buyer_device_signals{
      .log_context = log_context};
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(buyer_reporting_signals_json);
  ASSERT_TRUE(document.ok());
  TestSellerDeviceSignals observed_seller_device_signals =
      ParseSellerDeviceSignals(*document);
  if (!seller_dispatch_request_data.component_reporting_metadata
           .top_level_seller.empty()) {
    VerifyBuyerDeviceSignalsForComponentAuction(observed_seller_device_signals,
                                                seller_dispatch_request_data);
  } else {
    VerifySellerDeviceSignals(observed_seller_device_signals,
                              seller_dispatch_request_data);
  }
  ParseBuyerReportingSignals(observed_buyer_device_signals, *document);
  VerifyBuyerReportingSignals(observed_buyer_device_signals,
                              expected_buyer_dispatch_request_data);
}

void VerifyPASBuyerReportingSignalsJson(
    const std::string& buyer_reporting_signals_json,
    const BuyerReportingDispatchRequestData&
        expected_buyer_dispatch_request_data,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data) {
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  BuyerReportingDispatchRequestData observed_buyer_device_signals{
      .log_context = log_context};
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(buyer_reporting_signals_json);
  ASSERT_TRUE(document.ok());
  TestSellerDeviceSignals observed_seller_device_signals =
      ParseSellerDeviceSignals(*document);
  VerifySellerDeviceSignals(observed_seller_device_signals,
                            seller_dispatch_request_data);
  ParseBuyerReportingSignals(observed_buyer_device_signals, *document);
  VerifyBuyerReportingSignals(observed_buyer_device_signals,
                              expected_buyer_dispatch_request_data);
}

BuyerReportingDispatchRequestData GetTestBuyerDispatchRequestData(
    RequestLogContext& log_context) {
  std::shared_ptr<std::string> auction_config =
      std::make_shared<std::string>(kTestAuctionConfig);
  return {.auction_config = auction_config,
          .buyer_signals = kTestBuyerSignals,
          .join_count = kTestJoinCount,
          .recency = kTestRecency,
          .modeling_signals = kTestModelingSignals,
          .seller = kTestSeller,
          .interest_group_name = kTestInterestGroupName,
          .ad_cost = kTestAdCost,
          .buyer_reporting_id = kTestBuyerReportingId,
          .made_highest_scoring_other_bid = true,
          .log_context = log_context,
          .buyer_origin = kTestInterestGroupOwner,
          .signals_for_winner = kTestSignalsForWinner,
          .winning_ad_render_url = kTestRender,
          .egress_payload = kTestEgressPayload,
          .temporary_unlimited_egress_payload =
              kTestTemporaryUnlimitedEgressPayload};
}

}  // namespace privacy_sandbox::bidding_auction_servers
