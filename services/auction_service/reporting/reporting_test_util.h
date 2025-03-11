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

#ifndef SERVICES_AUCTION_SERVICE_REPORTING_TEST_UTIL_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_TEST_UTIL_H_

#include <string>

#include "rapidjson/document.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"

namespace privacy_sandbox::bidding_auction_servers {
struct TestSellerDeviceSignals {
  std::string top_window_hostname = kTestPublisherHostName;
  std::string render_url = kTestRender;
  std::string render_URL = kTestRender;
  float bid = kTestBuyerBid;
  uint32_t seller_data_version = kSellerDataVersion;
  std::string bid_currency = "EUR";
  std::string highest_scoring_other_bid_currency = "USD";
  float desirability = -1;
  float highest_scoring_other_bid = kTestHighestScoringOtherBid;
  std::optional<std::string> top_level_seller;
  std::optional<std::string> component_seller;
  std::optional<float> modified_bid;
};

TestSellerDeviceSignals ParseSellerDeviceSignals(rapidjson::Document& document);
void VerifySellerDeviceSignals(
    const TestSellerDeviceSignals& observed_seller_device_signals,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data);

// Sets console.log, console.warning, console.error console_logs in doc.
void SetAdTechLogs(const ReportingResponseLogs& console_logs,
                   rapidjson::Document& doc);
// Generates a ScoreAdsResponse for testing
ScoreAdsResponse::AdScore GetTestWinningScoreAdsResponse();
// Returns a serialized json for sellerDeviceSignals used in reportResult() and
// reportWin()'s buyerDeviceSignals()
rapidjson::Document GenerateTestSellerDeviceSignals(
    const SellerReportingDispatchRequestData& dispatch_request_data);
// Returns a serialized json for sellerDeviceSignals used in reportResult() and
// reportWin()'s buyerDeviceSignals() for component auctions.
SellerReportingDispatchRequestData
GetTestSellerDispatchRequestDataForComponentAuction(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context);
// Returns a serialized json for sellerDeviceSignals used in reportResult() and
// reportWin()'s buyerDeviceSignals() for top level auctions.
SellerReportingDispatchRequestData
GetTestSellerDispatchRequestDataForTopLevelAuction(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context);
// Compares observed and expected buyer's device signals.
void VerifyBuyerReportingSignals(
    BuyerReportingDispatchRequestData& observed_buyer_device_signals,
    const BuyerReportingDispatchRequestData& expected_buyer_device_signals);
// Parses buyerReportingSignals from document and sets values in
// reporting_dispatch_data.
void ParseBuyerReportingSignals(
    BuyerReportingDispatchRequestData& reporting_dispatch_data,
    rapidjson::Document& document);
// Generates a test SellerReportingDispatchRequestData based on
// PostAuctionSignals
SellerReportingDispatchRequestData GetTestSellerDispatchRequestData(
    PostAuctionSignals& post_auction_signals, RequestLogContext& log_context);
BuyerReportingDispatchRequestData GetTestBuyerDispatchRequestData(
    RequestLogContext& log_context);
void VerifyPABuyerReportingSignalsJson(
    const std::string& buyer_reporting_signals_json,
    const BuyerReportingDispatchRequestData&
        expected_buyer_dispatch_request_data,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data);
void VerifyPASBuyerReportingSignalsJson(
    const std::string& buyer_reporting_signals_json,
    const BuyerReportingDispatchRequestData&
        expected_buyer_dispatch_request_data,
    const SellerReportingDispatchRequestData& seller_dispatch_request_data);

void VerifyBuyerReportingUrl(const ScoreAdsResponse::AdScore& scored_ad);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_TEST_UTIL_H_
