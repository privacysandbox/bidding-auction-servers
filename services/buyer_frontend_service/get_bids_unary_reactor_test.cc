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

#include "services/buyer_frontend_service/get_bids_unary_reactor.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/synchronization/notification.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kSampleBuyerDebugId = "sample-buyer-debug-id";
constexpr absl::string_view kSampleGenerationId = "sample-seller-debug-id";

using ::testing::_;
using ::testing::AllOf;
using ::testing::An;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;

class GetBidUnaryReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::metric::ServerConfig config_proto;
    config_proto.set_mode(server_common::metric::ServerConfig::PROD);
    metric::BfeContextMap(
        server_common::metric::BuildDependentConfig(config_proto))
        ->Get(&request_);
  }

  grpc::CallbackServerContext context_;
  GetBidsRequest request_ = MakeARandomGetBidsRequest();
  GetBidsResponse response_;
  BiddingAsyncClientMock bidding_client_mock_;
  MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>
      bidding_signals_provider_;
  GetBidsConfig get_bids_config_;
};

TEST_F(GetBidUnaryReactorTest, LoadsBiddingSignalsAndCallsBiddingServer) {
  EXPECT_CALL(
      bidding_signals_provider_,
      Get(An<const BiddingSignalsRequest&>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<BiddingSignals>>) &&>>(),
          An<absl::Duration>()))
      .WillOnce([](const BiddingSignalsRequest& bidding_signals_request,
                   auto on_done, absl::Duration timeout) {
        std::move(on_done)(std::make_unique<BiddingSignals>());
      });

  absl::Notification notification;
  EXPECT_CALL(
      bidding_client_mock_,
      Execute(An<std::unique_ptr<GenerateBidsRequest>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GenerateBidsResponse>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest> get_values_request,
                    const RequestMetadata& metadata, auto on_done,
                    absl::Duration timeout) {
        std::move(on_done)(std::make_unique<GenerateBidsResponse>());
        notification.Notify();
        return absl::OkStatus();
      });

  get_bids_config_.encryption_enabled = false;
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, /*key_fetcher_manager=*/nullptr,
      /*crypto_client=*/nullptr);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServer_EncryptionEnabled) {
  request_.set_key_id("key_id");
  request_.set_request_ciphertext("ciphertext");

  EXPECT_CALL(
      bidding_signals_provider_,
      Get(An<const BiddingSignalsRequest&>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<BiddingSignals>>) &&>>(),
          An<absl::Duration>()))
      .WillOnce([](const BiddingSignalsRequest& bidding_signals_request,
                   auto on_done, absl::Duration timeout) {
        std::move(on_done)(std::make_unique<BiddingSignals>());
      });

  // Return an empty key.
  server_common::MockKeyFetcherManager key_fetcher_manager;
  server_common::PrivateKey private_key;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillOnce(testing::Return(private_key));

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse decrypt_response;
  decrypt_response.set_payload(request_.raw_request().SerializeAsString());
  decrypt_response.set_secret("secret");
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .WillOnce(testing::Return(decrypt_response));
  // Mock the AeadEncrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
  data.set_ciphertext("ciphertext");
  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse encrypt_response;
  *encrypt_response.mutable_encrypted_data() = std::move(data);
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .WillOnce(testing::Return(encrypt_response));

  absl::Notification notification;
  EXPECT_CALL(
      bidding_client_mock_,
      Execute(An<std::unique_ptr<GenerateBidsRequest>>(),
              An<const RequestMetadata&>(),
              An<absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                             GenerateBidsResponse>>) &&>>(),
              An<absl::Duration>()))
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest> get_values_request,
                    const RequestMetadata& metadata, auto on_done,
                    absl::Duration timeout) {
        std::move(on_done)(std::make_unique<GenerateBidsResponse>());
        notification.Notify();
        return absl::OkStatus();
      });

  get_bids_config_.encryption_enabled = true;
  GetBidsUnaryReactor class_under_test(context_, request_, response_,
                                       bidding_signals_provider_,
                                       bidding_client_mock_, get_bids_config_,
                                       &key_fetcher_manager, &crypto_client);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();

  EXPECT_FALSE(response_.has_raw_response());
  EXPECT_FALSE(response_.response_ciphertext().empty());
}

auto EqLogContext(const LogContext& log_context) {
  return AllOf(
      Property(&LogContext::generation_id, Eq(log_context.generation_id())),
      Property(&LogContext::adtech_debug_id,
               Eq(log_context.adtech_debug_id())));
}

auto EqGenerateBidsRawRequestWithLogContext(
    const GenerateBidsRequest::GenerateBidsRawRequest& raw_request) {
  return AllOf(
      Property(&GenerateBidsRequest::GenerateBidsRawRequest::log_context,
               EqLogContext(raw_request.log_context())));
}

auto EqGenerateBidsRequestWithLogContext(const GenerateBidsRequest& request_) {
  return AllOf(
      Property(&GenerateBidsRequest::raw_request,
               EqGenerateBidsRawRequestWithLogContext(request_.raw_request())));
}

TEST_F(GetBidUnaryReactorTest, VerifyLogContextPropagates) {
  auto* log_context = request_.mutable_raw_request()->mutable_log_context();
  log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  log_context->set_generation_id(kSampleGenerationId);

  EXPECT_CALL(
      bidding_signals_provider_,
      Get(An<const BiddingSignalsRequest&>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<BiddingSignals>>) &&>>(),
          An<absl::Duration>()))
      .WillOnce([](const BiddingSignalsRequest& bidding_signals_request,
                   auto on_done, absl::Duration timeout) {
        std::move(on_done)(std::make_unique<BiddingSignals>());
      });

  GenerateBidsRequest expected_generate_bids_request;
  auto* expected_log_context =
      expected_generate_bids_request.mutable_raw_request()
          ->mutable_log_context();
  expected_log_context->set_generation_id(kSampleGenerationId);
  expected_log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  EXPECT_CALL(bidding_client_mock_,
              Execute(Pointee(EqGenerateBidsRequestWithLogContext(
                          expected_generate_bids_request)),
                      _, _, _));

  get_bids_config_.encryption_enabled = false;
  GetBidsUnaryReactor get_bids_unary_reactor(
      context_, request_, response_, bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, /*key_fetcher_manager=*/nullptr,
      /*crypto_client=*/nullptr);
  get_bids_unary_reactor.Execute();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
