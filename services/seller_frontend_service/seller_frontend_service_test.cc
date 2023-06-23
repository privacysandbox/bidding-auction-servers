// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
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

#include "services/seller_frontend_service/seller_frontend_service.h"

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/server.h>

#include <gmock/gmock-matchers.h>
#include <include/gmock/gmock-actions.h>

#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/seller_frontend_config.pb.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kSampleInterestGroupName = "interest_group";
constexpr absl::string_view kSampleBuyer = "ad_tech_A.com";
constexpr absl::string_view kSampleGenerationId =
    "a8098c1a-f86e-11da-bd1a-00112444be1e";
constexpr absl::string_view kSamplePublisherName = "https://publisher.com";
constexpr int kNumberOfComponentAdUrls = 1;
constexpr absl::string_view kSampleSellerDomain = "https://sample-seller.com";
constexpr absl::string_view kSampleSellerSignals = "[]";
constexpr absl::string_view kSampleAuctionSignals = "[]";

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;
using GetBidDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetBidsResponse>>) &&>;
using ScoreAdsDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<ScoreAdsResponse>>) &&>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = ::google::protobuf::Map<std::string, BuyerInput>;

struct LocalSfeStartResult {
  int port;
  std::unique_ptr<grpc::Server> server;

  // Shutdown the server when the test is done.
  ~LocalSfeStartResult() {
    if (server) {
      server->Shutdown();
    }
  }
};

LocalSfeStartResult StartLocalSfe(
    SellerFrontEndService* seller_frontend_service) {
  grpc::ServerBuilder builder;
  int port;
  builder.AddListeningPort("[::]:0",
                           grpc::experimental::LocalServerCredentials(
                               grpc_local_connect_type::LOCAL_TCP),
                           &port);
  builder.RegisterService(seller_frontend_service);
  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  return {port, std::move(server)};
}

std::unique_ptr<SellerFrontEnd::StubInterface> CreateSfeStub(int port) {
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      absl::StrFormat("localhost:%d", port),
      grpc::experimental::LocalCredentials(grpc_local_connect_type::LOCAL_TCP));
  return SellerFrontEnd::NewStub(channel);
}

class SellerFrontEndServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::metric::ServerConfig config_proto;
    config_proto.set_mode(server_common::metric::ServerConfig::PROD);
    metric::SfeContextMap(
        server_common::metric::BuildDependentConfig(config_proto));
  }

  SellerFrontEndConfig config_;
};

TEST_F(SellerFrontEndServiceTest, ReturnsInvalidInputOnEmptyCiphertext) {
  config_.set_enable_encryption(true);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(MockAsyncProvider<BuyerBidsList, ScoringSignals>(),
                         ScoringAsyncClientMock(),
                         BuyerFrontEndAsyncClientFactoryMock(),
                         &key_fetcher_manager, std::move(async_reporter));
  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_client_type(SelectAdRequest::ANDROID);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kEmptyRemarketingCiphertextError);
}

TEST_F(SellerFrontEndServiceTest, ReturnsInternalErrorOnKeyNotFound) {
  config_.set_enable_encryption(true);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillOnce(Return(std::nullopt));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(MockAsyncProvider<BuyerBidsList, ScoringSignals>(),
                         ScoringAsyncClientMock(),
                         BuyerFrontEndAsyncClientFactoryMock(),
                         &key_fetcher_manager, std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_client_type(SelectAdRequest::ANDROID);
  // Set ciphertext to any non-null value for this test.
  request.set_protected_audience_ciphertext("q");
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kMissingPrivateKey);
}

TEST_F(SellerFrontEndServiceTest, ReturnsInvalidInputOnInvalidClientType) {
  config_.set_enable_encryption(true);
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  server_common::MockKeyFetcherManager key_fetcher_manager;
  ClientRegistry clients(MockAsyncProvider<BuyerBidsList, ScoringSignals>(),
                         ScoringAsyncClientMock(),
                         BuyerFrontEndAsyncClientFactoryMock(),
                         &key_fetcher_manager, std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_protected_audience_ciphertext("foo");
  request.set_client_type(SelectAdRequest::UNKNOWN);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_TRUE(
      absl::StrContains(status.error_message(), kUnsupportedClientType));
}

TEST_F(SellerFrontEndServiceTest, ReturnsInvalidInputOnEmptyBuyerList) {
  config_.set_enable_encryption(false);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(MockAsyncProvider<BuyerBidsList, ScoringSignals>(),
                         ScoringAsyncClientMock(),
                         BuyerFrontEndAsyncClientFactoryMock(),
                         &key_fetcher_manager, std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.mutable_raw_protected_audience_input()->set_generation_id(
      kSampleGenerationId);
  request.set_client_type(SelectAdRequest::ANDROID);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  std::vector<std::string> expected_errors = {
      kEmptyAuctionSignals, kEmptyBuyerList, kEmptySeller, kEmptySellerSignals};
  ASSERT_EQ(status.error_message(),
            absl::StrJoin(expected_errors, kErrorDelimiter));
}

TEST_F(SellerFrontEndServiceTest, SendsChaffOnMissingBuyerInputs) {
  // If ALL buyer inputs are missing, pending bids will decrease to 0
  // and we should abort the request. This test checks this behavior
  // assuming there is only a single buyer. If there are multiple buyers
  // and only a subset have missing inputs, the request should continue
  // with pending bids decreased by the amount of missing inputs.

  config_.set_enable_encryption(false);
  config_.set_seller_origin_domain(kSampleSellerDomain);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(MockAsyncProvider<BuyerBidsList, ScoringSignals>(),
                         ScoringAsyncClientMock(),
                         BuyerFrontEndAsyncClientFactoryMock(),
                         &key_fetcher_manager, std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_client_type(SelectAdRequest::ANDROID);
  request.mutable_auction_config()->mutable_buyer_list()->Add("Test");
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals("Test");
  request.mutable_auction_config()->set_auction_signals("Test");
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok());
  ASSERT_TRUE(response.raw_response().is_chaff());
}

TEST_F(SellerFrontEndServiceTest, SendsChaffOnMissingBuyerClient) {
  // If ALL buyer clients are missing, pending bids will decrease to 0
  // and we should mark the response as chaff. This test checks this behavior
  // assuming there is only a single buyer. If there are multiple buyers
  // and only a subset have missing clients, the request should continue
  // with pending bids decreased by the amount of missing clients.

  config_.set_enable_encryption(false);
  config_.set_seller_origin_domain(kSampleSellerDomain);

  BuyerFrontEndAsyncClientFactoryMock client_factory_mock;
  EXPECT_CALL(client_factory_mock, Get)
      .WillOnce([](absl::string_view buyer_ig_owner) { return nullptr; });
  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(MockAsyncProvider<BuyerBidsList, ScoringSignals>(),
                         ScoringAsyncClientMock(), client_factory_mock,
                         &key_fetcher_manager, std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_client_type(SelectAdRequest::BROWSER);
  request.mutable_raw_protected_audience_input()->set_generation_id(
      kSampleGenerationId);
  request.mutable_raw_protected_audience_input()->set_publisher_name(
      kSamplePublisherName);
  request.mutable_auction_config()->mutable_buyer_list()->Add(
      std::string(kSampleBuyer));
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->Add();

  // Because we set enable encryption to false, we will read from
  // raw_protected_audience_input.
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(kSampleBuyer, buyer_input);
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_inputs =
      GetEncodedBuyerInputMap(buyer_inputs);
  EXPECT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();
  *request.mutable_raw_protected_audience_input()->mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);

  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok());
  ASSERT_TRUE(response.raw_response().is_chaff());
}

TEST_F(SellerFrontEndServiceTest, SendsChaffOnEmptyGetBidsResponse) {
  config_.set_enable_encryption(false);
  config_.set_seller_origin_domain(kSampleSellerDomain);

  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->Add()->set_name(
      kSampleInterestGroupName);

  // Setup a SelectAdRequest with the aforementioned buyer input.
  SelectAdRequest request;
  DecodedBuyerInputs decoded_buyer_inputs;
  decoded_buyer_inputs.emplace(kSampleBuyer, buyer_input);
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_inputs =
      GetEncodedBuyerInputMap(decoded_buyer_inputs);
  EXPECT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();
  *request.mutable_raw_protected_audience_input()->mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);
  for (const auto& [local_buyer, unused] :
       request.raw_protected_audience_input().buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  request.mutable_raw_protected_audience_input()->set_publisher_name(
      MakeARandomString());
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  request.set_client_type(SelectAdRequest::BROWSER);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  EXPECT_EQ(request.auction_config().buyer_list_size(),
            request.raw_protected_audience_input().buyer_input_size());
  BuyerBidsList expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       request.raw_protected_audience_input().buyer_input()) {
    GetBidsResponse response;
    ASSERT_EQ(response.raw_response().bids().size(), 0);
    SetupBuyerClientMock(local_buyer, buyer_clients, response,
                         /*repeated_get_allowed=*/true);
    expected_buyer_bids.try_emplace(
        local_buyer, std::make_unique<GetBidsResponse>(response));
  }

  // Scoring signals provider
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  EXPECT_CALL(scoring_signals_provider, Get).Times(0);
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, Execute).Times(0);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(scoring_signals_provider, scoring_client,
                         buyer_clients, &key_fetcher_manager,
                         std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok());
  ASSERT_TRUE(response.raw_response().is_chaff());
}

TEST_F(SellerFrontEndServiceTest, RawRequestFinishWithSuccess) {
  config_.set_enable_encryption(false);
  config_.set_seller_origin_domain(kSampleSellerDomain);

  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->Add()->set_name(
      kSampleInterestGroupName);
  DecodedBuyerInputs decoded_buyer_inputs;
  decoded_buyer_inputs.emplace(kSampleBuyer, std::move(buyer_input));
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_inputs =
      GetEncodedBuyerInputMap(decoded_buyer_inputs);
  EXPECT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();

  // Setup a SelectAdRequest with the aforementioned buyer input.
  SelectAdRequest request;
  request.mutable_raw_protected_audience_input()->set_generation_id(
      kSampleGenerationId);
  *request.mutable_raw_protected_audience_input()->mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  for (const auto& [local_buyer, unused] :
       request.raw_protected_audience_input().buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  request.mutable_raw_protected_audience_input()->set_publisher_name(
      MakeARandomString());
  request.set_client_type(SelectAdRequest::BROWSER);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 1);
  int buyer_input_count =
      request.raw_protected_audience_input().buyer_input_size();
  EXPECT_EQ(buyer_input_count, 1);
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request);
  const float bid_value = 1.0;
  BuyerBidsList expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       request.raw_protected_audience_input().buyer_input()) {
    std::string url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid = BuildNewAdWithBid(url, kSampleInterestGroupName, bid_value,
                                      kNumberOfComponentAdUrls);
    GetBidsResponse response;
    auto mutable_bids = response.mutable_raw_response()->mutable_bids();
    mutable_bids->Add(std::move(bid));

    SetupBuyerClientMock(local_buyer, buyer_clients, response,
                         /*repeated_get_allowed=*/true);
    expected_buyer_bids.try_emplace(
        local_buyer, std::make_unique<GetBidsResponse>(response));
  }

  // Scoring signals provider
  std::string ad_render_urls = "test scoring signals";
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, Execute)
      .WillRepeatedly([](std::unique_ptr<ScoreAdsRequest> request,
                         const RequestMetadata& metadata,
                         ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        auto response = std::make_unique<ScoreAdsResponse>();
        std::move(on_done)(std::move(response));
        return absl::OkStatus();
      });
  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients(scoring_signals_provider, scoring_client,
                         buyer_clients, &key_fetcher_manager,
                         std::move(async_reporter));

  SellerFrontEndService seller_frontend_service(config_, clients);
  LocalSfeStartResult start_sfe_result =
      StartLocalSfe(&seller_frontend_service);
  std::unique_ptr<SellerFrontEnd::StubInterface> stub =
      CreateSfeStub(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
