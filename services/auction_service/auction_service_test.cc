//  Copyright 2022 Google LLC
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

#include "services/auction_service/auction_service.h"

#include <memory>

#include <grpcpp/server.h>

#include <gmock/gmock-matchers.h>

#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

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

LocalAuctionStartResult StartLocalAuction(AuctionService* auction_service) {
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

std::unique_ptr<Auction::StubInterface> CreateAuctionStub(int port) {
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      absl::StrFormat("localhost:%d", port),
      grpc::experimental::LocalCredentials(grpc_local_connect_type::LOCAL_TCP));
  return Auction::NewStub(channel);
}

TEST(AuctionServiceTest, InstantiatesScoreAdsReactor) {
  MockCodeDispatchClient dispatcher;
  ScoreAdsRequest request;
  ScoreAdsResponse response;

  absl::BlockingCounter init_pending(1);
  // initialize
  server_common::metric::ServerConfig config_proto;
  config_proto.set_mode(server_common::metric::ServerConfig::PROD);
  metric::AuctionContextMap(
      server_common::metric::BuildDependentConfig(config_proto));
  auto score_ads_reactor_factory =
      [&dispatcher, &init_pending](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger =
            std::make_unique<ScoreAdsNoOpLogger>();
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        auto mock = std::make_unique<MockScoreAdsReactor>(
            dispatcher, request, response, key_fetcher_manager, crypto_client,
            runtime_config, std::move(benchmarkingLogger),
            std::move(async_reporter), "");
        EXPECT_CALL(*mock, Execute).Times(1);
        init_pending.DecrementCount();
        return mock;
      };
  AuctionService service(std::move(score_ads_reactor_factory), nullptr, nullptr,
                         AuctionServiceRuntimeConfig());
  grpc::CallbackServerContext context;
  auto mock = service.ScoreAds(&context, &request, &response);
  init_pending.Wait();
  delete mock;
}

TEST(AuctionServiceTest, AbortsIfMissingAds) {
  MockCodeDispatchClient dispatcher;
  auto score_ads_reactor_factory =
      [&dispatcher](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            dispatcher, request, response,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client, std::move(async_reporter), runtime_config);
      };

  AuctionService service(std::move(score_ads_reactor_factory), nullptr, nullptr,
                         AuctionServiceRuntimeConfig());

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  ScoreAdsRequest request;
  ScoreAdsResponse response;
  grpc::Status status = stub->ScoreAds(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kNoAdsToScore);
}

TEST(AuctionServiceTest, AbortsIfMissingScoringSignals) {
  MockCodeDispatchClient dispatcher;
  auto score_ads_reactor_factory =
      [&dispatcher](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            dispatcher, request, response,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client, std::move(async_reporter), runtime_config);
      };
  AuctionService service(std::move(score_ads_reactor_factory), nullptr, nullptr,
                         AuctionServiceRuntimeConfig());

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  ScoreAdsRequest request;
  *request.mutable_raw_request()->mutable_ad_bids()->Add() =
      MakeARandomAdWithBidMetadata(1, 10);
  ScoreAdsResponse response;
  grpc::Status status = stub->ScoreAds(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kNoTrustedScoringSignals);
}

TEST(AuctionServiceTest, AbortsIfMissingDispatchRequests) {
  MockCodeDispatchClient dispatcher;
  auto score_ads_reactor_factory =
      [&dispatcher](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            dispatcher, request, response,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client, std::move(async_reporter), runtime_config);
      };
  AuctionService service(std::move(score_ads_reactor_factory), nullptr, nullptr,
                         AuctionServiceRuntimeConfig());

  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);

  grpc::ClientContext context;
  ScoreAdsRequest request;
  *request.mutable_raw_request()->mutable_ad_bids()->Add() =
      MakeARandomAdWithBidMetadata(1, 10);
  request.mutable_raw_request()->set_scoring_signals(
      R"json({"renderUrls":{"placeholder_url":[123]}})json");
  ScoreAdsResponse response;
  grpc::Status status = stub->ScoreAds(&context, request, &response);

  ASSERT_EQ(status.error_message(), kNoAdsWithValidScoringSignals);
  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
