
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
#include "services/auction_service/reporting/seller/component_seller_reporting_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_response.h"
#include "services/auction_service/reporting/reporting_test_util.h"
#include "services/auction_service/reporting/seller/seller_reporting_manager.h"
#include "services/common/test/mocks.h"
#include "services/common/util/json_util.h"
namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr absl::string_view kExpectedSellerDeviceSignals =
    R"JSON({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"bidCurrency":"EUR","dataVersion":1989,"highestScoringOtherBidCurrency":"USD","desirability":2.0,"highestScoringOtherBid":0.5,"topLevelSeller":"testTopLevelSeller","modifiedBid":1.0})JSON";

TEST(TestSellerReportingManager, ReturnsRapidJsonDocOfSellerDeviceSignals) {
  ScoreAdsResponse::AdScore winning_ad_score = GetTestWinningScoreAdsResponse();
  PostAuctionSignals post_auction_signals = GeneratePostAuctionSignals(
      winning_ad_score, kUsdIsoCode, kSellerDataVersion);
  RequestLogContext log_context(/*context_map=*/{},
                                server_common::ConsentedDebugConfiguration());
  SellerReportingDispatchRequestData dispatch_request_data =
      GetTestSellerDispatchRequestDataForComponentAuction(post_auction_signals,
                                                          log_context);
  rapidjson::Document json_doc =
      GenerateSellerDeviceSignalsForComponentAuction(dispatch_request_data);
  absl::StatusOr<std::string> generatedjson = SerializeJsonDoc(json_doc);
  ASSERT_TRUE(generatedjson.ok());
  EXPECT_EQ(*generatedjson, kExpectedSellerDeviceSignals);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
