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

#include "services/bidding_service/bidding_service.h"

#include <memory>
#include <vector>

#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/bidding_service/generate_bids_reactor_test_utils.h"
#include "services/bidding_service/protected_app_signals_generate_bids_reactor.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/service_utils.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using GenerateProtectedAppSignalsBidsRawResponse =
    GenerateProtectedAppSignalsBidsResponse::
        GenerateProtectedAppSignalsBidsRawResponse;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse;
using server_common::KeyFetcherManagerInterface;
using ::testing::NiceMock;

class BiddingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    config_.SetOverride(kTrue, TEST_MODE);
  }

  MockV8DispatchClient v8_dispatch_client_;
  std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger_ =
      std::make_unique<BiddingNoOpLogger>();
  TrustedServersConfigClient config_{{}};
  BiddingServiceRuntimeConfig runtime_config_;
  std::unique_ptr<NiceMock<MockCryptoClientWrapper>> crypto_client_ =
      std::make_unique<NiceMock<MockCryptoClientWrapper>>();
};

TEST_F(BiddingServiceTest, InstantiatesGenerateBidsReactor) {
  GenerateBidsRequest request;
  GenerateBidsResponse response;
  absl::BlockingCounter init_pending(1);
  // initialize
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  metric::MetricContextMap<google::protobuf::Message>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
  std::unique_ptr<NiceMock<MockCryptoClientWrapper>> crypto_client =
      std::make_unique<NiceMock<MockCryptoClientWrapper>>();
  BiddingService service(
      [this, &init_pending](
          grpc::CallbackServerContext* context,
          const GenerateBidsRequest* request, GenerateBidsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
            std::make_unique<BiddingNoOpLogger>();
        auto mock = std::make_unique<MockGenerateBidsReactor>(
            context, v8_dispatch_client_, request, response, "",
            std::move(benchmarkingLogger), key_fetcher_manager, crypto_client,
            runtime_config);
        EXPECT_CALL(*mock, Execute).Times(1);
        init_pending.DecrementCount();
        return mock.release();
      },
      CreateKeyFetcherManager(config_, /*public_key_fetcher=*/nullptr),
      std::move(crypto_client), runtime_config_,
      [this](grpc::CallbackServerContext* context,
             const GenerateProtectedAppSignalsBidsRequest* request,
             const BiddingServiceRuntimeConfig& runtime_config,
             GenerateProtectedAppSignalsBidsResponse* response,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client,
             KVAsyncClient* ad_retrieval_async_client,
             KVAsyncClient* kv_async_client,
             EgressSchemaCache* egress_schema_cache,
             EgressSchemaCache* limited_egress_schema_cache) {
        auto mock =
            std::make_unique<GenerateProtectedAppSignalsMockBidsReactor>(
                context, v8_dispatch_client_, runtime_config, request, response,
                key_fetcher_manager, crypto_client, ad_retrieval_async_client,
                kv_async_client, egress_schema_cache,
                limited_egress_schema_cache);
        EXPECT_CALL(*mock, Execute).Times(0);
        return mock.release();
      });
  grpc::CallbackServerContext context;
  auto mock = std::unique_ptr<grpc::ServerUnaryReactor>(
      service.GenerateBids(&context, &request, &response));
  init_pending.Wait();
}

TEST_F(BiddingServiceTest, EncryptsResponseEvenOnException) {
  absl::BlockingCounter init_pending(1);
  // Expect that response gets encrypted.
  EXPECT_CALL(*crypto_client_, AeadEncrypt).Times(1).WillOnce([]() {
    return AeadEncryptResponse{};
  });
  BiddingService bidding_service(
      [this, &init_pending](
          grpc::CallbackServerContext* context,
          const GenerateBidsRequest* request, GenerateBidsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const BiddingServiceRuntimeConfig& runtime_config) {
        auto generate_bids_reactor = std::make_unique<GenerateBidsReactor>(
            context, v8_dispatch_client_, request, response,
            std::move(benchmarking_logger_), key_fetcher_manager, crypto_client,
            runtime_config);
        init_pending.DecrementCount();
        return generate_bids_reactor.release();
      },
      CreateKeyFetcherManager(config_, /*public_key_fetcher=*/nullptr),
      std::move(crypto_client_), runtime_config_,
      [this](grpc::CallbackServerContext* context,
             const GenerateProtectedAppSignalsBidsRequest* request,
             const BiddingServiceRuntimeConfig& runtime_config,
             GenerateProtectedAppSignalsBidsResponse* response,
             server_common::KeyFetcherManagerInterface* key_fetcher_manager,
             CryptoClientWrapperInterface* crypto_client,
             KVAsyncClient* ad_retrieval_async_client,
             KVAsyncClient* kv_async_client,
             EgressSchemaCache* egress_schema_cache,
             EgressSchemaCache* limited_egress_schema_cache) {
        auto mock =
            std::make_unique<GenerateProtectedAppSignalsMockBidsReactor>(
                context, v8_dispatch_client_, runtime_config, request, response,
                key_fetcher_manager, crypto_client, ad_retrieval_async_client,
                kv_async_client, egress_schema_cache,
                limited_egress_schema_cache);
        EXPECT_CALL(*mock, Execute).Times(0);
        return mock.release();
      });
  LocalServiceStartResult start_service_result =
      StartLocalService(&bidding_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  grpc::ClientContext context;
  GenerateBidsRequest request;
  GenerateBidsResponse response;
  grpc::Status status = stub->GenerateBids(&context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

class BiddingProtectedAppSignalsTest : public BiddingServiceTest {
 protected:
  void SetUp() override {
    config_.SetOverride(kTrue, TEST_MODE);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_APP_SIGNALS);
    runtime_config_ = {.is_protected_app_signals_enabled = true};
    SetupMockCryptoClientWrapper(*crypto_client_);
    server_common::GrpcInit gprc_init;
  }

  BiddingService CreateBiddingService() {
    return BiddingService(
        [this](grpc::CallbackServerContext* context,
               const GenerateBidsRequest* request,
               GenerateBidsResponse* response,
               server_common::KeyFetcherManagerInterface* key_fetcher_manager,
               CryptoClientWrapperInterface* crypto_client,
               const BiddingServiceRuntimeConfig& runtime_config) {
          auto generate_bids_reactor = std::make_unique<GenerateBidsReactor>(
              context, v8_dispatch_client_, request, response,
              std::move(benchmarking_logger_), key_fetcher_manager,
              crypto_client, runtime_config);
          return generate_bids_reactor.release();
        },
        CreateKeyFetcherManager(config_, /*public_key_fetcher=*/nullptr),
        std::move(crypto_client_), runtime_config_,
        [this](grpc::CallbackServerContext* context,
               const GenerateProtectedAppSignalsBidsRequest* request,
               const BiddingServiceRuntimeConfig& runtime_config,
               GenerateProtectedAppSignalsBidsResponse* response,
               server_common::KeyFetcherManagerInterface* key_fetcher_manager,
               CryptoClientWrapperInterface* crypto_client,
               KVAsyncClient* ad_retrieval_async_client,
               KVAsyncClient* kv_async_client,
               EgressSchemaCache* egress_schema_cache,
               EgressSchemaCache* limited_egress_schema_cache) {
          auto reactor =
              std::make_unique<ProtectedAppSignalsGenerateBidsReactor>(
                  context, v8_dispatch_client_, runtime_config, request,
                  response, key_fetcher_manager, crypto_client,
                  ad_retrieval_async_client, kv_async_client,
                  egress_schema_cache, limited_egress_schema_cache);
          return reactor.release();
        },
        std::move(ad_retrieval_client_),
        /*kv_async_client=*/nullptr, std::move(egress_schema_cache_),
        std::move(limited_egress_schema_cache_));
  }

  std::unique_ptr<EgressSchemaCacheMock> CreateEgressSchemaCacheMock() {
    auto cddl_spec_cache = std::make_unique<CddlSpecCache>(
        "services/bidding_service/egress_cddl_spec/");
    CHECK_OK(cddl_spec_cache->Init());
    return std::make_unique<EgressSchemaCacheMock>(std::move(cddl_spec_cache));
  }

  void SetPASMetricContext(GenerateProtectedAppSignalsBidsRequest& request) {
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request);
  }

  std::unique_ptr<KVAsyncClientMock> ad_retrieval_client_ =
      std::make_unique<KVAsyncClientMock>();
  std::unique_ptr<EgressSchemaCacheMock> egress_schema_cache_ =
      CreateEgressSchemaCacheMock();
  std::unique_ptr<EgressSchemaCacheMock> limited_egress_schema_cache_ =
      CreateEgressSchemaCacheMock();
  int num_roma_requests_ = 0;
};

TEST_F(BiddingProtectedAppSignalsTest, NormalWorkflowWorks) {
  SetupProtectedAppSignalsRomaExpectations(
      v8_dispatch_client_, num_roma_requests_,
      CreatePrepareDataForAdsRetrievalResponse(),
      CreateGenerateBidsUdfResponse(kTestRenderUrl, kTestWinningBid));
  SetupAdRetrievalClientExpectations(*ad_retrieval_client_);
  auto bidding_service = CreateBiddingService();
  LocalServiceStartResult start_service_result =
      StartLocalService(&bidding_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  grpc::ClientContext context;
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto request = CreateProtectedAppSignalsRequest(raw_request);
  SetPASMetricContext(request);
  GenerateProtectedAppSignalsBidsResponse response;
  grpc::Status status =
      stub->GenerateProtectedAppSignalsBids(&context, request, &response);

  ASSERT_TRUE(status.ok()) << status.error_message();
  GenerateProtectedAppSignalsBidsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  ASSERT_EQ(raw_response.bids().size(), 1);
  const auto& bid = raw_response.bids()[0];
  EXPECT_EQ(bid.bid(), kTestWinningBid);
  EXPECT_EQ(bid.render(), kTestRenderUrl);
}

TEST_F(BiddingProtectedAppSignalsTest,
       BadPrepareDataForAdRetrievalResponseFinishesRpc) {
  EXPECT_CALL(v8_dispatch_client_, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback batch_callback) {
        // First dispatch happens for `prepareDataForAdRetrieval` UDF.
        return absl::InternalError(kInternalServerError);
      });
  EXPECT_CALL(*ad_retrieval_client_, ExecuteInternal).Times(0);
  auto bidding_service = CreateBiddingService();
  LocalServiceStartResult start_service_result =
      StartLocalService(&bidding_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  grpc::ClientContext context;
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto request = CreateProtectedAppSignalsRequest(raw_request);
  SetPASMetricContext(request);
  GenerateProtectedAppSignalsBidsResponse response;
  grpc::Status status =
      stub->GenerateProtectedAppSignalsBids(&context, request, &response);

  ASSERT_FALSE(status.ok());
}

TEST_F(BiddingProtectedAppSignalsTest, BadAdRetrievalResponseFinishesRpc) {
  EXPECT_CALL(v8_dispatch_client_, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback batch_callback) {
        // First dispatch happens for `prepareDataForAdRetrieval` UDF.
        return MockRomaExecution(batch, std::move(batch_callback),
                                 kPrepareDataForAdRetrievalEntryFunctionName,
                                 kPrepareDataForAdRetrievalBlobVersion,
                                 CreatePrepareDataForAdsRetrievalResponse());
      });
  EXPECT_CALL(*ad_retrieval_client_, ExecuteInternal)
      .WillOnce([](std::unique_ptr<GetValuesRequest> raw_request,
                   grpc::ClientContext* context,
                   absl::AnyInvocable<void(
                       absl::StatusOr<std::unique_ptr<GetValuesResponse>>,
                       ResponseMetadata)&&>
                       on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        return absl::InternalError(kInternalServerError);
      });
  auto bidding_service = CreateBiddingService();
  LocalServiceStartResult start_service_result =
      StartLocalService(&bidding_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  grpc::ClientContext context;
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto request = CreateProtectedAppSignalsRequest(raw_request);
  SetPASMetricContext(request);
  GenerateProtectedAppSignalsBidsResponse response;
  grpc::Status status =
      stub->GenerateProtectedAppSignalsBids(&context, request, &response);

  ASSERT_FALSE(status.ok());
}

TEST_F(BiddingProtectedAppSignalsTest, BadGenerateBidResponseFinishesRpc) {
  EXPECT_CALL(v8_dispatch_client_, BatchExecute)
      .WillRepeatedly([this](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback batch_callback) {
        ++num_roma_requests_;
        if (num_roma_requests_ == 1) {
          // First dispatch happens for `prepareDataForAdRetrieval` UDF.
          return MockRomaExecution(batch, std::move(batch_callback),
                                   kPrepareDataForAdRetrievalEntryFunctionName,
                                   kPrepareDataForAdRetrievalBlobVersion,
                                   CreatePrepareDataForAdsRetrievalResponse());
        } else if (num_roma_requests_ == 2) {
          // Second dispatch happens for `generateBid` UDF.
          return absl::InternalError(kInternalServerError);
        }
        return absl::OkStatus();
      });
  SetupAdRetrievalClientExpectations(*ad_retrieval_client_);
  auto bidding_service = CreateBiddingService();
  LocalServiceStartResult start_service_result =
      StartLocalService(&bidding_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  grpc::ClientContext context;
  auto raw_request = CreateRawProtectedAppSignalsRequest(
      kTestAuctionSignals, kTestBuyerSignals,
      CreateProtectedAppSignals(kTestAppInstallSignals, kTestEncodingVersion),
      kTestSeller, kTestPublisherName);
  auto request = CreateProtectedAppSignalsRequest(raw_request);
  SetPASMetricContext(request);
  GenerateProtectedAppSignalsBidsResponse response;
  grpc::Status status =
      stub->GenerateProtectedAppSignalsBids(&context, request, &response);

  ASSERT_FALSE(status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
