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

#ifndef SERVICES_AUCTION_SERVICE_INTEGRATION_TEST_UTIL_H_
#define SERVICES_AUCTION_SERVICE_INTEGRATION_TEST_UTIL_H_

#include <string>

#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kTestSeller = "http://seller.com";
constexpr absl::string_view kTestInterestGroupName = "testInterestGroupName";
constexpr double kTestAdCost = 2.0;
constexpr long kTestRecency = 3;
constexpr int kTestModelingSignals = 4;
constexpr int kTestJoinCount = 5;
constexpr absl::string_view kTestBuyerSignals = R"([1,"test",[2]])";
constexpr absl::string_view kTestAuctionSignals = R"([[3,"test",[4]]])";

struct TestBuyerReportingSignals {
  absl::string_view seller = kTestSeller;
  absl::string_view interest_group_name = kTestInterestGroupName;
  double ad_cost = kTestAdCost;
  long recency = kTestRecency;
  int modeling_signals = kTestModelingSignals;
  int join_count = kTestJoinCount;
  absl::string_view buyer_signals = kTestBuyerSignals;
  absl::string_view auction_signals = kTestAuctionSignals;
};

struct TestScoreAdsRequestConfig {
  const TestBuyerReportingSignals& test_buyer_reporting_signals;
  bool enable_debug_reporting = false;
  int desired_ad_count = 90;
  std::string top_level_seller = "";
  std::string component_level_seller = "";
  bool is_consented = false;
  std::optional<std::string> buyer_reporting_id;
  std::string interest_group_owner = "";
};

// This function simulates a E2E successful call to ScoreAds in the
// Auction Service E2E for Protected Audience:
// - Loads a test protected audience udf for buyer and seller
// into Roma
// - Creates a test ScoreAdsRequest based on the TestScoreAdsConfig.
// - Calls ScoreAd using the request
// - Sets the response
void LoadAndRunScoreAdsForPA(
    const AuctionServiceRuntimeConfig& runtime_config,
    const TestScoreAdsRequestConfig& test_score_ads_request_config,
    ScoreAdsResponse& response);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_INTEGRATION_TEST_UTIL_H_
