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

#include "services/buyer_frontend_service/buyer_frontend_service.h"

#include <gmock/gmock-matchers.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/server.h>

#include <include/gmock/gmock-actions.h>

#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/util/buyer_frontend_test_utils.h"
#include "services/common/chaffing/transcoding_utils.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/service_utils.h"
#include "services/common/test/utils/test_utils.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::_;
using ::testing::An;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::Return;
using MockBiddingSignalsProvider =
    MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>;
using GenerateBidsRawResponse = GenerateBidsResponse::GenerateBidsRawResponse;
using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;
using GenerateProtectedAppSignalsBidsRawResponse =
    GenerateProtectedAppSignalsBidsResponse::
        GenerateProtectedAppSignalsBidsRawResponse;
using GetBidsRawResponse = GetBidsResponse::GetBidsRawResponse;

constexpr char valid_bidding_signals[] =
    R"JSON({"keys":{"key":[123,456]}})JSON";

TrustedServersConfigClient CreateTrustedServerConfigClient() {
  TrustedServersConfigClient config_client({});

  config_client.SetOverride(kTrue, TEST_MODE);
  return config_client;
}

// TODO: Put this in a common place and reuse across tests.
std::unique_ptr<MockCryptoClientWrapper> CreateCryptoClient() {
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  EXPECT_CALL(*crypto_client, HpkeEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly(
          [](const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
             const std::string& plaintext_payload) {
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kTestSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(
                kTestKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                plaintext_payload);
            return hpke_encrypt_response;
          });

  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.
  EXPECT_CALL(*crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        *hpke_decrypt_response.mutable_payload() = ciphertext;
        hpke_decrypt_response.set_secret(kTestSecret);
        return hpke_decrypt_response;
      });

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.
  EXPECT_CALL(*crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly(
          [](const std::string& plaintext_payload, const std::string& secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            *data.mutable_ciphertext() = plaintext_payload;
            ABSL_LOG(INFO) << "AeadEncrypt sending response back: "
                           << plaintext_payload;
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });
  return crypto_client;
}

ClientRegistry CreateClientRegistry(
    std::unique_ptr<MockBiddingSignalsProvider> bidding_signals_async_provider,
    std::unique_ptr<BiddingAsyncClientMock> bidding_async_client,
    std::unique_ptr<ProtectedAppSignalsBiddingAsyncClientMock>
        protected_app_signals_bidding_async_client) {
  auto config_client = CreateTrustedServerConfigClient();
  return {.bidding_signals_async_provider =
              std::move(bidding_signals_async_provider),
          .bidding_async_client = std::move(bidding_async_client),
          .protected_app_signals_bidding_async_client =
              std::move(protected_app_signals_bidding_async_client),
          .key_fetcher_manager = CreateKeyFetcherManager(
              config_client, CreatePublicKeyFetcher(config_client)),
          .crypto_client = CreateCryptoClient()};
}

GetBidsConfig CreateGetBidsConfig() {
  return {.protected_app_signals_generate_bid_timeout_ms = 60000,
          .is_protected_app_signals_enabled = true,
          .is_protected_audience_enabled = true};
}

class BuyerFrontEndServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<GetBidsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);
  }

  BiddingServiceClientConfig bidding_service_client_config_;
  grpc::ClientContext client_context_;
  GetBidsRequest request_;
  GetBidsResponse response_;
};

std::unique_ptr<BiddingAsyncClientMock> GetValidBiddingAsyncClientMock() {
  std::unique_ptr<BiddingAsyncClientMock> bidding_async_client =
      std::make_unique<BiddingAsyncClientMock>();
  EXPECT_CALL(
      *bidding_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<GenerateBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([](std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                       get_values_raw_request,
                   grpc::ClientContext* context, auto on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto raw_response = std::make_unique<GenerateBidsRawResponse>();
        *raw_response->mutable_bids()->Add() = CreateAdWithBid();
        std::move(on_done)(std::move(raw_response),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      });
  return bidding_async_client;
}

std::unique_ptr<BiddingAsyncClientMock>
GetValidBiddingAsyncClientMockNotCalled() {
  std::unique_ptr<BiddingAsyncClientMock> bidding_async_client =
      std::make_unique<BiddingAsyncClientMock>();
  EXPECT_CALL(*bidding_async_client, ExecuteInternal).Times(0);
  return bidding_async_client;
}

std::unique_ptr<ProtectedAppSignalsBiddingAsyncClientMock>
GetValidProtectedAppSignalsBiddingClientMock() {
  auto protected_app_signals_bidding_async_client =
      std::make_unique<ProtectedAppSignalsBiddingAsyncClientMock>();
  EXPECT_CALL(
      *protected_app_signals_bidding_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([](std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
                       get_values_raw_request,
                   grpc::ClientContext* context, auto on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        auto raw_response =
            std::make_unique<GenerateProtectedAppSignalsBidsRawResponse>();
        *raw_response->mutable_bids()->Add() =
            CreateProtectedAppSignalsAdWithBid();
        std::move(on_done)(std::move(raw_response),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      });
  return protected_app_signals_bidding_async_client;
}

TEST_F(BuyerFrontEndServiceTest,
       ProtectedAudienceAndProtectedAppSignalsBidsFetched) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetValidBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetValidProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 1);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 1);
}

TEST_F(
    BuyerFrontEndServiceTest,
    NoProtectedAudienceButSomeProtectedAppSignalsBidsFetchedForEmptySignals) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/"",
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = std::make_unique<BiddingAsyncClientMock>();
  // We never expect the Bidding client to be called; this call should be
  // skipped for lack of trusted bidding signals.
  EXPECT_CALL(*bidding_async_client, ExecuteInternal).Times(0);
  auto protected_app_signals_bidding_async_client =
      GetValidProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 1);
}

std::unique_ptr<MockBiddingSignalsProvider>
GetBiddingSignalsProviderErrorMock() {
  auto bidding_signals_async_provider =
      std::make_unique<MockBiddingSignalsProvider>();
  EXPECT_CALL(*bidding_signals_async_provider,
              Get(An<const BiddingSignalsRequest&>(),
                  An<absl::AnyInvocable<
                      void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                           GetByteSize) &&>>(),
                  An<absl::Duration>(), _))
      .WillOnce([](const BiddingSignalsRequest& bidding_signals_request,
                   auto on_done, absl::Duration timeout,
                   RequestContext context) {
        GetByteSize get_byte_size;
        std::move(on_done)(absl::InternalError(kInternalServerError),
                           get_byte_size);
      });
  return bidding_signals_async_provider;
}

TEST_F(BuyerFrontEndServiceTest,
       BiddingSignalsErrorAndProtectedAppSignalsBidsFetched) {
  auto bidding_signals_async_provider = GetBiddingSignalsProviderErrorMock();
  auto bidding_async_client = GetValidBiddingAsyncClientMockNotCalled();
  auto protected_app_signals_bidding_async_client =
      GetValidProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 1);
}

std::unique_ptr<BiddingAsyncClientMock> GetErrorBiddingAsyncClientMock() {
  std::unique_ptr<BiddingAsyncClientMock> bidding_async_client =
      std::make_unique<BiddingAsyncClientMock>();
  EXPECT_CALL(
      *bidding_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<GenerateBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([](std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                       get_values_raw_request,
                   grpc::ClientContext* context, auto on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(absl::Status(absl::StatusCode::kInvalidArgument,
                                        kRejectionReasonBidBelowAuctionFloor),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      });
  return bidding_async_client;
}

TEST_F(BuyerFrontEndServiceTest,
       ProtectedAudienceBiddingErrorButProtectedAppSignalsBidsFetched) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetErrorBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetValidProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 1);
}

std::unique_ptr<ProtectedAppSignalsBiddingAsyncClientMock>
GetErrorProtectedAppSignalsBiddingClientMock() {
  auto protected_app_signals_bidding_async_client =
      std::make_unique<ProtectedAppSignalsBiddingAsyncClientMock>();
  EXPECT_CALL(
      *protected_app_signals_bidding_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([](std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
                       get_values_raw_request,
                   grpc::ClientContext* context, auto on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(absl::Status(absl::StatusCode::kInvalidArgument,
                                        kRejectionReasonCategoryExclusions),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      });
  return protected_app_signals_bidding_async_client;
}

TEST_F(BuyerFrontEndServiceTest,
       ProtectedAudienceBidFetchedButProtectedAppSignalsBidsErrored) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetValidBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetErrorProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 1);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 0);
}

std::unique_ptr<BiddingAsyncClientMock> GetEmptyBiddingAsyncClientMock() {
  std::unique_ptr<BiddingAsyncClientMock> bidding_async_client =
      std::make_unique<BiddingAsyncClientMock>();
  EXPECT_CALL(
      *bidding_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<GenerateBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([](std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                       get_values_raw_request,
                   grpc::ClientContext* context, auto on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(std::make_unique<GenerateBidsRawResponse>(),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      });
  return bidding_async_client;
}

TEST_F(BuyerFrontEndServiceTest,
       ProtectedAudienceBidEmptyButProtectedAppSignalsBidsFetched) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetEmptyBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetValidProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 1);
}

std::unique_ptr<ProtectedAppSignalsBiddingAsyncClientMock>
GetEmptyProtectedAppSignalsBiddingClientMock() {
  auto protected_app_signals_bidding_async_client =
      std::make_unique<ProtectedAppSignalsBiddingAsyncClientMock>();
  EXPECT_CALL(
      *protected_app_signals_bidding_async_client,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([](std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
                       get_values_raw_request,
                   grpc::ClientContext* context, auto on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateProtectedAppSignalsBidsRawResponse>(),
            /* response_metadata= */ {});
        return absl::OkStatus();
      });
  return protected_app_signals_bidding_async_client;
}

TEST_F(BuyerFrontEndServiceTest,
       ProtectedAudienceBidFetchedButProtectedAppSignalsBidsEmpty) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetValidBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetEmptyProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 1);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 0);
}

TEST_F(BuyerFrontEndServiceTest,
       BothProtectedAudienceBidAndProtectedAppSignalsBidsEmptyIsOk) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetEmptyBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetEmptyProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 0);
}

TEST_F(BuyerFrontEndServiceTest,
       ProtectedAudienceBidEmptyAndProtectedAppSignalsBidsErrorIsOk) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetEmptyBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetErrorProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 0);
}

TEST_F(
    BuyerFrontEndServiceTest,
    ProtectedAudienceBidErrorAndProtectedAppSignalsBidErrorMeansOverallError) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetErrorBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetErrorProtectedAppSignalsBiddingClientMock();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_FALSE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 0);
  EXPECT_THAT(status.error_message(),
              HasSubstr(kRejectionReasonBidBelowAuctionFloor));
  EXPECT_THAT(status.error_message(),
              HasSubstr(kRejectionReasonCategoryExclusions));
}

std::unique_ptr<ProtectedAppSignalsBiddingAsyncClientMock>
GetProtectedAppSignalsBiddingClientMockThatWillNotBeCalled() {
  auto protected_app_signals_bidding_async_client =
      std::make_unique<ProtectedAppSignalsBiddingAsyncClientMock>();
  EXPECT_CALL(*protected_app_signals_bidding_async_client, ExecuteInternal)
      .Times(0);
  return protected_app_signals_bidding_async_client;
}

TEST_F(BuyerFrontEndServiceTest,
       RPCFinishesEvenWhenProtectedAppSignalsAreNotProvided) {
  auto bidding_signals_async_provider = SetupBiddingProviderMock(
      /*bidding_signals_value=*/valid_bidding_signals,
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt);
  auto bidding_async_client = GetValidBiddingAsyncClientMock();
  auto protected_app_signals_bidding_async_client =
      GetProtectedAppSignalsBiddingClientMockThatWillNotBeCalled();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/false,
                                  /*add_protected_audience_input=*/true);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 1);
  EXPECT_EQ(raw_response.protected_app_signals_bids_size(), 0);
}

TEST_F(BuyerFrontEndServiceTest,
       RPCErrorsWhenProtectedAppSignalsAndProtectedAudienceNotProvided) {
  auto bidding_signals_async_provider = std::make_unique<
      MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>();
  EXPECT_CALL(*bidding_signals_async_provider, Get).Times(0);
  auto bidding_async_client = std::make_unique<BiddingAsyncClientMock>();
  EXPECT_CALL(*bidding_async_client, ExecuteInternal).Times(0);
  auto protected_app_signals_bidding_async_client =
      GetProtectedAppSignalsBiddingClientMockThatWillNotBeCalled();

  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      CreateGetBidsConfig());
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/false,
                                  /*add_protected_audience_input=*/false);
  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  ASSERT_FALSE(status.ok()) << server_common::ToAbslStatus(status);
  EXPECT_THAT(status.error_message(), HasSubstr(kMissingInputs));
}

TEST_F(BuyerFrontEndServiceTest,
       ThrowsInvalidInputOnMalformedCiphertext_NewRequestFormat) {
  auto bidding_signals_async_provider = std::make_unique<
      MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>();
  EXPECT_CALL(*bidding_signals_async_provider, Get).Times(0);
  auto bidding_async_client = std::make_unique<BiddingAsyncClientMock>();
  EXPECT_CALL(*bidding_async_client, ExecuteInternal).Times(0);
  auto protected_app_signals_bidding_async_client =
      GetProtectedAppSignalsBiddingClientMockThatWillNotBeCalled();

  GetBidsConfig config = CreateGetBidsConfig();
  config.is_chaffing_enabled = true;
  BuyerFrontEndService buyer_frontend_service(
      CreateClientRegistry(
          std::move(bidding_signals_async_provider),
          std::move(bidding_async_client),
          std::move(protected_app_signals_bidding_async_client)),
      config);

  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(true);
  raw_request.mutable_log_context()->set_generation_id(kTestGenerationId);
  absl::StatusOr<std::string> encoded_payload =
      EncodeAndCompressGetBidsPayload(raw_request, CompressionType::kGzip, 999);
  for (int i = 0; i < 5; i++) (*encoded_payload)[i] = 'q';

  *request_.mutable_request_ciphertext() = *std::move(encoded_payload);
  *request_.mutable_key_id() = "key_id";

  auto start_bfe_result = StartLocalService(&buyer_frontend_service);
  auto stub = CreateServiceStub<BuyerFrontEnd>(start_bfe_result.port);
  grpc::Status status = stub->GetBids(&client_context_, request_, &response_);

  absl::Status absl_status = server_common::ToAbslStatus(status);
  ASSERT_FALSE(status.ok()) << server_common::ToAbslStatus(status);
  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
