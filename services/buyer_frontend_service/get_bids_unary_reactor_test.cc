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
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kSampleBuyerDebugId = "sample-buyer-debug-id";
constexpr absl::string_view kSampleGenerationId = "sample-seller-debug-id";

using ::testing::_;
using ::testing::AllOf;
using ::testing::An;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;

constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillRepeatedly(
          [](const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
             const std::string& plaintext_payload) {
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                plaintext_payload);
            return hpke_encrypt_response;
          });

  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        *hpke_decrypt_response.mutable_payload() = ciphertext;
        hpke_decrypt_response.set_secret(kSecret);
        return hpke_decrypt_response;
      });

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const std::string& plaintext_payload,
                         const std::string& secret) {
        google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
        *data.mutable_ciphertext() = plaintext_payload;
        VLOG(1) << "AeadEncrypt sending response back: " << plaintext_payload;
        google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
            aead_encrypt_response;
        *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
        return aead_encrypt_response;
      });
}

class GetBidUnaryReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::metric::ServerConfig config_proto;
    config_proto.set_mode(server_common::metric::ServerConfig::PROD);
    metric::BfeContextMap(
        server_common::metric::BuildDependentConfig(config_proto))
        ->Get(&request_);
    get_bids_config_.encryption_enabled = true;

    TrustedServersConfigClient config_client({});
    config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_client.SetFlagForTest(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(config_client);
    SetupMockCryptoClientWrapper(*crypto_client_);
  }

  grpc::CallbackServerContext context_;
  GetBidsRequest request_ = MakeARandomGetBidsRequest();
  GetBidsRequest::GetBidsRawRequest raw_request_ =
      MakeARandomGetBidsRawRequest();
  GetBidsResponse response_;
  BiddingAsyncClientMock bidding_client_mock_;
  MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>
      bidding_signals_provider_;
  GetBidsConfig get_bids_config_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
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
      ExecuteInternal(
          An<std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>>(),
          An<const RequestMetadata&>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateBidsResponse::GenerateBidsRawResponse>>) &&>>(),
          An<absl::Duration>()))
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    const RequestMetadata& metadata, auto on_done,
                    absl::Duration timeout) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>());
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get());
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServer_EncryptionEnabled) {
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
      ExecuteInternal(
          An<std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>>(),
          An<const RequestMetadata&>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateBidsResponse::GenerateBidsRawResponse>>) &&>>(),
          An<absl::Duration>()))
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_request,
                    const RequestMetadata& metadata, auto on_done,
                    absl::Duration timeout) {
        auto raw_response =
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>();
        raw_response->mutable_bids()->Add();
        std::move(on_done)(std::move(raw_response));
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get());
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();

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

TEST_F(GetBidUnaryReactorTest, VerifyLogContextPropagates) {
  auto* log_context = raw_request_.mutable_log_context();
  log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  log_context->set_generation_id(kSampleGenerationId);
  *request_.mutable_request_ciphertext() = raw_request_.SerializeAsString();

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

  GenerateBidsRequest::GenerateBidsRawRequest
      expected_generate_bids_raw_request;
  auto* expected_log_context =
      expected_generate_bids_raw_request.mutable_log_context();
  expected_log_context->set_generation_id(kSampleGenerationId);
  expected_log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  EXPECT_CALL(bidding_client_mock_,
              ExecuteInternal(Pointee(EqGenerateBidsRawRequestWithLogContext(
                                  expected_generate_bids_raw_request)),
                              _, _, _));

  GetBidsUnaryReactor get_bids_unary_reactor(
      context_, request_, response_, bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get());
  get_bids_unary_reactor.Execute();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
