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

#include <memory>
#include <string>
#include <utility>

#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kTestSeller = "http://seller.com";
constexpr absl::string_view kTestComponentSeller = "http://componentseller.com";
constexpr absl::string_view kTestInterestGroupName = "testInterestGroupName";
constexpr double kTestAdCost = 2.0;
constexpr long kTestRecency = 3;
constexpr int kTestModelingSignals = 4;
constexpr int kTestJoinCount = 5;
constexpr absl::string_view kTestBuyerSignals = R"([1,"test",[2]])";
constexpr absl::string_view kTestAuctionSignals = R"([[3,"test",[4]]])";
constexpr uint32_t kTestDataVersion = 1689;
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
    "buyerAndSellerReportingId=undefined&adCost=2&highestScoringOtherBid=0&"
    "madeHighestScoringOtherBid=false&signalsForWinner="
    "{\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&auctionSignals="
    "3,test,4&desirability=undefined&topLevelSeller=topLevelSeller&"
    "modifiedBid=undefined&dataVersion=1689";
constexpr inline char kTestInteractionEvent[] = "clickEvent";
constexpr inline char kTestInteractionReportingUrl[] = "http://click.com";
constexpr char kTestIgOwner[] = "barStandardAds.com";
TestComponentAuctionResultData GenerateTestComponentAuctionResultData();

struct LocalAuctionStartResult {
  int port;
  std::unique_ptr<grpc::Server> server;

  // Shutdown the server when the test is done.
  ~LocalAuctionStartResult() {
    if (server) {
      server->Shutdown();
    }
  }
};

inline LocalAuctionStartResult StartLocalAuction(
    AuctionService* auction_service) {
  grpc::ServerBuilder builder;
  int port;
  builder.AddListeningPort("[::]:0",
                           grpc::experimental::LocalServerCredentials(
                               grpc_local_connect_type::LOCAL_TCP),
                           &port);
  builder.RegisterService(auction_service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  return {port, std::move(server)};
}

inline std::unique_ptr<Auction::StubInterface> CreateAuctionStub(int port) {
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      absl::StrFormat("localhost:%d", port),
      grpc::experimental::LocalCredentials(grpc_local_connect_type::LOCAL_TCP));
  return Auction::NewStub(channel);
}

struct TestBuyerReportingSignals {
  absl::string_view seller = kTestSeller;
  absl::string_view interest_group_name = kTestInterestGroupName;
  double ad_cost = kTestAdCost;
  long recency = kTestRecency;
  int modeling_signals = kTestModelingSignals;
  int join_count = kTestJoinCount;
  absl::string_view buyer_signals = kTestBuyerSignals;
  absl::string_view auction_signals = kTestAuctionSignals;
  uint32_t data_version = kTestDataVersion;
};

struct TestScoreAdsRequestConfig {
  const TestBuyerReportingSignals& test_buyer_reporting_signals;
  bool enable_debug_reporting = false;
  int desired_ad_count = 90;
  std::string top_level_seller = "";
  bool is_consented = false;
  std::optional<std::string> buyer_reporting_id;
  std::optional<std::string> buyer_and_seller_reporting_id;
  std::string interest_group_owner = "";
  TestComponentAuctionResultData component_auction_data;
  const uint32_t seller_data_version = kTestSellerDataVersion;
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
    absl::string_view buyer_udf, absl::string_view seller_udf,
    ScoreAdsResponse& response);

void LoadAndRunScoreAdsForPAS(
    const AuctionServiceRuntimeConfig& runtime_config,
    const TestScoreAdsRequestConfig& test_score_ads_request_config,
    absl::string_view buyer_udf, absl::string_view seller_udf,
    ScoreAdsResponse& response);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_INTEGRATION_TEST_UTIL_H_
