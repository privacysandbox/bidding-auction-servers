/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_AUCTION_SERVICE_AUCTION_TEST_CONSTANTS_H_
#define SERVICES_AUCTION_SERVICE_AUCTION_TEST_CONSTANTS_H_

#include <cstdint>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kTestSeller = "http://seller.com";
constexpr absl::string_view kTestComponentSeller = "http://componentseller.com";
constexpr absl::string_view kTestInterestGroupName = "testInterestGroupName";
constexpr double kTestAdCost = 2.0;
constexpr int kTestModelingSignals = 4;
constexpr int kTestJoinCount = 5;
constexpr int kTestIgIdx = 1;
constexpr absl::string_view kTestBuyerSignalsArr = R"([1,"test",[2]])";
constexpr absl::string_view kTestAuctionSignalsArr = R"([[3,"test",[4]]])";
constexpr uint32_t kTestSellerDataVersion = 1989;
constexpr inline char kExpectedComponentReportResultUrl[] =
    "http://"
    "test.com&bid=1&bidCurrency=EUR&dataVersion=1989&highestScoringOtherBid=0&"
    "highestScoringOtherBidCurrency=???&topWindowHostname=fenceStreetJournal."
    "com&interestGroupOwner=barStandardAds.com&topLevelSeller=topLevelSeller&"
    "modifiedBid=2";
constexpr inline char kTestGenerationId[] = "generationId";
constexpr inline char kExpectedComponentReportWinUrl[] =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&buyerReportingId=buyerReportingId&"
    "buyerAndSellerReportingId=undefined&selectedBuyerAndSellerReportingId="
    "undefined&adCost=2&highestScoringOtherBid=0&madeHighestScoringOtherBid="
    "false&signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,"
    "test,2&auctionSignals=3,test,4&desirability=undefined&topLevelSeller="
    "topLevelSeller&modifiedBid=undefined&dataVersion=1689";
constexpr inline char kTestInteractionEvent[] = "clickEvent";
constexpr inline char kTestInteractionReportingUrl[] = "http://click.com";
constexpr char kTestInteractionUrl[] = "http://event.com";
constexpr char kTestIgOwner[] = "barStandardAds.com";
constexpr char kTestReportResultUrl[] =
    "http://"
    "test.com&bid=1&bidCurrency=EUR&dataVersion=1989&highestScoringOtherBid=1&"
    "highestScoringOtherBidCurrency=???&topWindowHostname=fenceStreetJournal."
    "com&interestGroupOwner=barStandardAds.com&buyerAndSellerReportingId="
    "undefined&selectedBuyerAndSellerReportingId=undefined";
constexpr char kTestReportWinUrl[] =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=testInterestGroupName&adCost=2&"
    "modelingSignals=4&recency=2&joinCount=5";
constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
constexpr char kTestBuyerAndSellerReportingId[] =
    "testBuyerAndSellerReportingId";
constexpr char kTestSelectedBuyerAndSellerReportingId[] =
    "testSelectedBuyerAndSellerReportingId";
constexpr char kMinimalReportWinUrl[] = "http://reportWinUrl.com";
constexpr char kMinimalReportWinUrlWithBuyerReportingId[] =
    "http://reportWinUrl.com&buyerReportingId=testId";
constexpr char kTestLog[] = "testLog";
constexpr char kTestSignalsForWinner[] = "{\"testKey\":\"publisherName\"}";
constexpr bool kSendReportToInvokedTrue = true;
constexpr bool kRegisterAdBeaconInvokedTrue = true;
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
constexpr char kTestRender[] = "http://testurl.com";
constexpr float kTestBuyerBid = 1.0;
constexpr float kTestDesirability = 2.0;
constexpr float kTestHighestScoringOtherBid = 0.5;
constexpr char kEnableAdtechCodeLoggingTrue[] = "true";
constexpr float kTestRecency = 2.1;
constexpr char kTestBuyerMetadata[] =
    R"({"enableReportWinUrlGeneration":true,"enableProtectedAppSignals":false,"perBuyerSignals":{"testkey":"testvalue"},"buyerOrigin":"testOwner","madeHighestScoringOtherBid":true,"joinCount":5,"recency":2,"modelingSignals":4,"seller":"http://seller.com","adCost":2.0,"interestGroupName":"testInterestGroupName","dataVersion":1689})";
constexpr char kTestBuyerMetadataWithProtectedAppSignals[] =
    R"({"enableReportWinUrlGeneration":true,"enableProtectedAppSignals":true,"perBuyerSignals":{"testkey":"testvalue"},"buyerOrigin":"testOwner","madeHighestScoringOtherBid":true,"joinCount":5,"recency":2,"modelingSignals":4,"seller":"http://seller.com","interestGroupName":"testInterestGroupName","adCost":2.0})";
constexpr char kTestBuyerSignalsObj[] = "{\"testkey\":\"testvalue\"}";
constexpr char kTestTopLevelSeller[] = "testTopLevelSeller";
constexpr float kTestModifiedBid = 1.0;
constexpr char kUsdIsoCode[] = "USD";
constexpr char kEurosIsoCode[] = "EUR";
constexpr bool kTestEnableReportWinInputNoisingTrue = true;
constexpr char kTestEgressPayload[] = "testEgressPayload";
constexpr char kTestTemporaryUnlimitedEgressPayload[] =
    "testTemporaryUnlimitedEgressPayload";
constexpr char kTestKAnonStatus[] = "passedNotEnforced";
constexpr uint32_t kTestDataVersion = 1689;  // NOLINT
constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr bool kEnableReportResultUrlGenerationFalse = false;
constexpr bool kEnableReportResultWinGenerationFalse = false;
constexpr char kTestConsentToken[] = "testConsentToken";
constexpr char kPublisherHostname[] = "fenceStreetJournal.com";
constexpr char kDefaultBarInterestGroupOwner[] = "barStandardAds.com";

constexpr float kTestBid = 2.10;
constexpr char kTestProtectedAppSignalsAdOwner[] = "https://PAS-Ad-Owner.com";
constexpr char kTestTopLevelReportResultUrlInResponse[] =
    "http://reportResultUrl.com";
constexpr char kTestReportingWinResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"clickEvent":"http://event.com"}},"sellerLogs":["testLog"], "sellerErrors":["testLog"], "sellerWarnings":["testLog"],
"reportWinResponse":{"reportWinUrl":"http://reportWinUrl.com","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"clickEvent":"http://event.com"}},"buyerLogs":["testLog"], "buyerErrors":["testLog"], "buyerWarnings":["testLog"]})";
constexpr char kTestReportingResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"clickEvent":"http://event.com"}},"sellerLogs":["testLog"]})";
constexpr char kEmptyTestReportingResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"","sendReportToInvoked":false,"registerAdBeaconInvoked":false,"interactionReportingUrls":{}},"sellerLogs":[], "sellerErrors":[], "sellerWarnings":[])";
constexpr char kTestReportResultResponseJson[] =
    R"({"response":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","interactionReportingUrls":{"clickEvent":"http://event.com"}},"logs":["testLog"], "errors":["testLog"], "warnings":["testLog"]})";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test 2"})json";
constexpr char kTestScoringSignals[] = R"json(
  {
    "renderUrls": {
      "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0": [
          123
        ],
      "barStandardAds.com/render_ad?id=bar": [
          "barScoringSignalValue1",
          "barScoringSignalValue2"
        ],
      "barStandardAds.com/render_ad?id=barbecue": [
          1689,
          1868
        ],
      "barStandardAds.com/render_ad?id=barbecue2": [
          1689,
          1868
        ],
      "fooMeOnceAds.com/render_ad?id=hasFooComp": [
          1689,
          1868
        ],
      "bootScootin.com/render_ad?id=cowboy_boots": [
          1689,
          1868
        ]
    },
    "adComponentRenderUrls": {
      "adComponent.com/foo_components/id=0":["foo0"],
      "adComponent.com/foo_components/id=1":["foo1"],
      "adComponent.com/foo_components/id=2":["foo2"],
      "adComponent.com/bar_components/id=0":["bar0"],
      "adComponent.com/bar_components/id=1":["bar1"],
      "adComponent.com/bar_components/id=2":["bar2"]
    }
  }
)json";

constexpr char kTestReportWinResponseJson[] =
    R"({"response":{"reportWinUrl":"http://reportWinUrl.com","interactionReportingUrls":{"clickEvent":"http://event.com"}},"logs":["testLog"], "errors":["testLog"], "warnings":["testLog"]})";

constexpr char kTestReportWinUrlWithSignals[] =
    "http://test.com?seller=http://"
    "seller.com&adCost=2&modelingSignals=4&recency=2&"
    "madeHighestScoringOtherBid=true&joinCount=5&signalsForWinner={"
    "\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&auctionSignals=3,"
    "test,4&desirability=undefined&dataVersion=1689";

constexpr char kTestReportWinUrlWithBuyerReportingId[] =
    "http://test.com?seller=http://"
    "seller.com&adCost=2&modelingSignals=4&recency=2&"
    "madeHighestScoringOtherBid=true&joinCount=5&signalsForWinner={"
    "\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&auctionSignals=3,"
    "test,4&desirability=undefined&dataVersion=1689&buyerReportingId="
    "testBuyerReportingId";

constexpr char kTestReportWinUrlWithNoising[] =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=testInterestGroupName&adCost=2&"
    "madeHighestScoringOtherBid=true&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&"
    "auctionSignals=3,test,4&desirability=undefined&dataVersion=1689";

constexpr char kTestComponentWinReportingUrl[] =
    "http://componentReportingUrl.com";
constexpr char kTestComponentEvent[] = "clickEvent";
constexpr char kTestComponentInteractionReportingUrl[] =
    "http://componentInteraction.com";
constexpr char kTestComponentIgOwner[] = "http://componentIgOwner.com";
constexpr absl::string_view kExpectedReportResultUrl =
    "http://"
    "test.com&bid=1&bidCurrency=EUR&dataVersion=1989&highestScoringOtherBid=0&"
    "highestScoringOtherBidCurrency=???&topWindowHostname=fenceStreetJournal."
    "com&interestGroupOwner=barStandardAds.com&buyerAndSellerReportingId="
    "undefined&selectedBuyerAndSellerReportingId=undefined";
constexpr absl::string_view
    kExpectedReportResultUrlWithBuyerAndSellerReportingId =
        "http://"
        "test.com&bid=1&bidCurrency=EUR&dataVersion=1989&"
        "highestScoringOtherBid=0&"
        "highestScoringOtherBidCurrency=???&topWindowHostname="
        "fenceStreetJournal."
        "com&interestGroupOwner=barStandardAds.com&buyerAndSellerReportingId="
        "buyerAndSellerReportingId&selectedBuyerAndSellerReportingId=undefined";
constexpr absl::string_view kExpectedReportResultUrlWithSelectedReportingId =
    "http://"
    "test.com&bid=1&bidCurrency=EUR&dataVersion=1989&highestScoringOtherBid=0&"
    "highestScoringOtherBidCurrency=???&topWindowHostname=fenceStreetJournal."
    "com&interestGroupOwner=barStandardAds.com&buyerAndSellerReportingId="
    "buyerAndSellerReportingId&selectedBuyerAndSellerReportingId="
    "selectedBuyerAndSellerReportingId";
constexpr absl::string_view kExpectedReportWinUrl =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&buyerReportingId=buyerReportingId&"
    "buyerAndSellerReportingId=undefined&selectedBuyerAndSellerReportingId="
    "undefined&adCost=2&highestScoringOtherBid=0&madeHighestScoringOtherBid="
    "false&signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,"
    "test,2&auctionSignals=3,test,4&desirability=undefined&topLevelSeller="
    "undefined&modifiedBid=undefined&dataVersion=1689";
constexpr absl::string_view kExpectedReportWinUrlWithNullSignalsForWinner =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&buyerReportingId=buyerReportingId&"
    "buyerAndSellerReportingId=undefined&selectedBuyerAndSellerReportingId="
    "undefined&adCost=2&highestScoringOtherBid=0&madeHighestScoringOtherBid="
    "false&signalsForWinner=null&perBuyerSignals=1,test,2&auctionSignals=3,"
    "test,4&desirability=undefined&topLevelSeller=undefined&modifiedBid="
    "undefined&dataVersion=1689";
constexpr absl::string_view kExpectedReportWinUrlWithBuyerAndSellerReportingId =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&buyerReportingId=undefined&"
    "buyerAndSellerReportingId=buyerAndSellerReportingId&"
    "selectedBuyerAndSellerReportingId=undefined&adCost=2&"
    "highestScoringOtherBid=0&madeHighestScoringOtherBid=false&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&"
    "auctionSignals=3,test,4&desirability=undefined&topLevelSeller=undefined&"
    "modifiedBid=undefined&dataVersion=1689";
constexpr absl::string_view kExpectedReportWinUrlWithSelectedReportingId =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&buyerReportingId=buyerReportingId&"
    "buyerAndSellerReportingId=buyerAndSellerReportingId&"
    "selectedBuyerAndSellerReportingId=selectedBuyerAndSellerReportingId&"
    "adCost=2&highestScoringOtherBid=0&madeHighestScoringOtherBid=false&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&"
    "auctionSignals=3,test,4&desirability=undefined&topLevelSeller=undefined&"
    "modifiedBid=undefined&dataVersion=1689";
constexpr absl::string_view kExpectedReportWinWithEmptyPerBuyerConfig =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&buyerReportingId=undefined&"
    "buyerAndSellerReportingId=buyerAndSellerReportingId&"
    "selectedBuyerAndSellerReportingId=undefined&adCost=2&"
    "highestScoringOtherBid=0&madeHighestScoringOtherBid=false&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=undefined&"
    "auctionSignals=3,test,4&desirability=undefined&topLevelSeller=undefined&"
    "modifiedBid=undefined&dataVersion=1689";
constexpr absl::string_view kExpectedReportWinWithKAnonStatus =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=testInterestGroupName&buyerReportingId="
    "undefined&buyerAndSellerReportingId=undefined&adCost=2&"
    "highestScoringOtherBid=0&madeHighestScoringOtherBid=false&kAnonStatus="
    "passedAndEnforced&signalsForWinner={\"testSignal\":\"testValue\"}&"
    "perBuyerSignals=1,test,2&auctionSignals=3,test,4&desirability=undefined&"
    "topLevelSeller=undefined&modifiedBid=undefined&dataVersion=1689";
constexpr absl::string_view kTestTopLevelReportResultUrl =
    "http://"
    "test.com&bid=1&bidCurrency=undefined&highestScoringOtherBid=undefined&"
    "highestScoringOtherBidCurrency=undefined&topWindowHostname="
    "fenceStreetJournal.com&interestGroupOwner=barStandardAds.com";
constexpr absl::string_view kTestSellerCodeVersion = "test_bucket";
constexpr char kTestUrl[] = "https://test-ads-url.com";
constexpr char kTestSellerSignals[] =
    R"json({"seller_signal": "test_seller_signals"})json";
constexpr char kTestAuctionSignalsObj[] =
    R"json({"auction_signal": "test_auction_signals"})json";
constexpr char kTestPublisherHostname[] = "test_publisher_hostname";
constexpr char kRomaTestError[] = "roma_test_error";
constexpr absl::string_view kTestRenderUrl =
    "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0";
constexpr absl::string_view kFooInterestGroupOwner = "https://fooAds.com";
constexpr char kInterestGroupOrigin[] = "candy_smush";
constexpr int kTestNumOfComponentAds = 3;
constexpr char kTestMetadataKey[] = "arbitraryMetadataKey";
constexpr int kTestAdMetadataValue = 2;
constexpr char kTestAdComponentRenderUrlBase[] =
    "adComponent.com/foo_components/id=";
constexpr char kAdMetadataPropertyNameRenderUrl[] = "renderUrl";
constexpr char kAdMetadataPropertyNameMetadata[] = "metadata";
constexpr char kInterestGroupOwnerOfBarBidder[] = "barStandardAds.com";
constexpr char kBarRenderUrl[] = "barStandardAds.com/render_ad?id=bar";
constexpr char kBarbecureIgName[] = "barbecue_lovers";
constexpr char kEuroIsoCode[] = "EUR";
constexpr char kTestTemporaryEgressPayload[] = "testTemporaryEgressPayload";
constexpr char kTestProtectedAppSignalsRenderUrl[] =
    "testAppAds.com/render_ad?id=bar";
constexpr char kTestProtectedAppScoringSignals[] = R"json(
  {
    "renderUrls": {
      "testAppAds.com/render_ad?id=bar": ["test_signal"]
    }
  }
)json";
constexpr char kTestAdRenderUrl[] =
    "https://adtech?adg_id=142601302539&cr_id=628073386727&cv_id=0";
constexpr absl::string_view kDebugLossUrlForZeroethIg =
    "https://example-ssp.com/debugLoss/0";
constexpr absl::string_view kDebugWinUrlForFirstIg =
    "https://example-ssp.com/debugWin/1";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_AUCTION_TEST_CONSTANTS_H_
