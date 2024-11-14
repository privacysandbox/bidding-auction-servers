//  Copyright 2023 Google LLC
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

#ifndef SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_TEST_CONSTANTS_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_TEST_CONSTANTS_H_

#include <cstdint>

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kTestReportResultUrl[] = "http://reportResultUrl.com";
constexpr char kTestReportWinUrl[] = "http://reportWinUrl.com";
constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
constexpr char kTestBuyerAndSellerReportingId[] =
    "testBuyerAndSellerReportingId";
constexpr char kTestReportWinUrlWithBuyerReportingId[] =
    "http://reportWinUrl.com&buyerReportingId=testId";
constexpr char kTestLog[] = "testLog";
constexpr char kTestSignalsForWinner[] = "{\"testKey\":\"publisherName\"}";
constexpr bool kSendReportToInvokedTrue = true;
constexpr bool kRegisterAdBeaconInvokedTrue = true;
constexpr char kTestInteractionEvent[] = "click";
constexpr char kTestInteractionUrl[] = "http://event.com";
constexpr char kTestPublisherHostName[] = "publisherName";
constexpr char kTestAuctionConfig[] = "testAuctionConfig";
constexpr uint32_t kSellerDataVersion = 1989;
constexpr char kTestSellerReportingSignalsForComponentSeller[] =
    R"({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"bidCurrency":"EUR","dataVersion":1989,"highestScoringOtherBidCurrency":"???","desirability":2.0,"highestScoringOtherBid":0.5,"topLevelSeller":"testTopLevelSeller","modifiedBid":1.0,"modifiedBidCurrency":"USD","componentSeller":"http://seller.com"})";
constexpr char kTestSellerReportingSignalsWithOtherBidCurrency[] =
    R"({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"bidCurrency":"EUR","dataVersion":1989,"highestScoringOtherBidCurrency":"USD","desirability":2.0,"highestScoringOtherBid":0.5})";
constexpr char kTestSellerReportingSignals[] =
    R"({"topWindowHostname":"publisherName","interestGroupOwner":"testOwner","renderURL":"http://testurl.com","renderUrl":"http://testurl.com","bid":1.0,"bidCurrency":"???","dataVersion":1989,"highestScoringOtherBidCurrency":"???","desirability":2.0,"highestScoringOtherBid":0.5})";
constexpr char kTestInterestGroupOwner[] = "testOwner";
constexpr char kTestInterestGroupName[] = "testInterestGroupName";
constexpr char kTestRender[] = "http://testurl.com";
constexpr float kTestBuyerBid = 1.0;
constexpr float kTestDesirability = 2.0;
constexpr float kTestHighestScoringOtherBid = 0.5;
constexpr char kEnableAdtechCodeLoggingTrue[] = "true";
constexpr int kTestJoinCount = 1;
constexpr float kTestRecency = 2.1;
constexpr int kTestModelingSignals = 3;
constexpr char kTestBuyerMetadata[] =
    R"({"enableReportWinUrlGeneration":true,"enableProtectedAppSignals":false,"perBuyerSignals":{"testkey":"testvalue"},"buyerOrigin":"testOwner","madeHighestScoringOtherBid":true,"joinCount":1,"recency":2,"modelingSignals":3,"seller":"http://seller.com","adCost":5.0,"interestGroupName":"testInterestGroupName","dataVersion":1648})";
constexpr char kTestBuyerMetadataWithProtectedAppSignals[] =
    R"({"enableReportWinUrlGeneration":true,"enableProtectedAppSignals":true,"perBuyerSignals":{"testkey":"testvalue"},"buyerOrigin":"testOwner","madeHighestScoringOtherBid":true,"joinCount":1,"recency":2,"modelingSignals":3,"seller":"http://seller.com","interestGroupName":"testInterestGroupName","adCost":5.0})";
constexpr char kTestBuyerSignals[] = "{\"testkey\":\"testvalue\"}";
constexpr char kTestSeller[] = "http://seller.com";
constexpr double kTestAdCost = 5.0;
constexpr char kTestTopLevelSeller[] = "testTopLevelSeller";
constexpr float kTestModifiedBid = 1.0;
constexpr char kUsdIsoCode[] = "USD";
constexpr char kEurosIsoCode[] = "EUR";
constexpr bool kTestEnableReportWinInputNoisingTrue = true;
constexpr char kTestEgressPayload[] = "testEgressPayload";
constexpr char kTestTemporaryUnlimitedEgressPayload[] =
    "testTemporaryUnlimitedEgressPayload";
constexpr uint32_t kTestDataVersion = 1648;  // NOLINT
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_REPORTING_HELPER_TEST_CONSTANTS_H_
