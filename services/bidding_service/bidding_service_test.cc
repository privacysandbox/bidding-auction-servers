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

#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/service_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse;
using ::testing::NiceMock;

TEST(BiddingServiceTest, InstantiatesGenerateBidsReactor) {
  MockCodeDispatchClient client;
  GenerateBidsRequest request;
  GenerateBidsResponse response;
  absl::BlockingCounter init_pending(1);
  // initialize
  server_common::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::TelemetryConfig::PROD);
  metric::BiddingContextMap(server_common::BuildDependentConfig(config_proto));
  BiddingServiceRuntimeConfig bidding_service_runtime_config = {
      .encryption_enabled = true};
  TrustedServersConfigClient config{{}};
  config.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config.SetFlagForTest(kTrue, TEST_MODE);
  BiddingServiceRuntimeConfig runtime_config_ = {.encryption_enabled = true};
  std::unique_ptr<NiceMock<MockCryptoClientWrapper>> crypto_client =
      std::make_unique<NiceMock<MockCryptoClientWrapper>>();
  BiddingService service(
      [&client, &init_pending](
          const GenerateBidsRequest* request, GenerateBidsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const BiddingServiceRuntimeConfig& runtime_config) {
        std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
            std::make_unique<BiddingNoOpLogger>();
        auto mock = new MockGenerateBidsReactor(
            client, request, response, "", std::move(benchmarkingLogger),
            key_fetcher_manager, crypto_client, runtime_config);
        EXPECT_CALL(*mock, Execute).Times(1);
        init_pending.DecrementCount();
        return mock;
      },
      CreateKeyFetcherManager(config), std::move(crypto_client),
      bidding_service_runtime_config);
  grpc::CallbackServerContext context;
  auto mock = service.GenerateBids(&context, &request, &response);
  init_pending.Wait();
  delete mock;
}

class BiddingServer : public ::testing::Test {
 protected:
  void SetUp() override {
    config_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_.SetFlagForTest(kTrue, TEST_MODE);
  }

  V8Dispatcher dispatcher_;
  NiceMock<CodeDispatchClient> code_dispatch_client_{dispatcher_};
  std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger_ =
      std::make_unique<BiddingNoOpLogger>();
  TrustedServersConfigClient config_{{}};
  BiddingServiceRuntimeConfig runtime_config_ = {.encryption_enabled = true};
  std::unique_ptr<NiceMock<MockCryptoClientWrapper>> crypto_client_ =
      std::make_unique<NiceMock<MockCryptoClientWrapper>>();
};

TEST_F(BiddingServer, EncryptsResponseEvenOnException) {
  MockCodeDispatchClient client;
  absl::BlockingCounter init_pending(1);
  // Expect that response gets encrypted.
  EXPECT_CALL(*crypto_client_, AeadEncrypt).Times(1).WillOnce([]() {
    return AeadEncryptResponse{};
  });
  BiddingService bidding_service(
      [&client, &init_pending, this](
          const GenerateBidsRequest* request, GenerateBidsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const BiddingServiceRuntimeConfig& runtime_config) {
        auto generate_bids_reactor = std::make_unique<GenerateBidsReactor>(
            client, request, response, std::move(benchmarking_logger_),
            key_fetcher_manager, crypto_client, runtime_config);
        init_pending.DecrementCount();
        return generate_bids_reactor.release();
      },
      CreateKeyFetcherManager(config_), std::move(crypto_client_),
      runtime_config_);
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

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
