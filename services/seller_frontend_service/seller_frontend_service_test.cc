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

#include <gmock/gmock-matchers.h>

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/server.h>

#include <include/gmock/gmock-actions.h>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/test/utils/service_utils.h"
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::string_view kSampleInterestGroupName = "interest_group";
constexpr absl::string_view kSampleBuyer = "ad_tech_A.com";
constexpr absl::string_view kSampleGenerationId =
    "a8098c1a-f86e-11da-bd1a-00112444be1e";
constexpr absl::string_view kSamplePublisherName = "https://publisher.com";
constexpr absl::string_view kSampleSellerDomain = "https://sample-seller.com";
constexpr absl::string_view kSampleSellerSignals = "[]";
constexpr absl::string_view kSampleAuctionSignals = "[]";
constexpr absl::string_view kSampleAdRenderUrl =
    "https://adtechads.com/relevant_ad";
constexpr absl::string_view kErrorIntendedForAdServer =
    "Error intended for ad server";

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;
using GetBidDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>) &&>;
using ScoreAdsDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                ScoreAdsResponse::ScoreAdsRawResponse>>) &&>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = ::google::protobuf::Map<std::string, BuyerInput>;

template <typename T>
class SellerFrontEndServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        server_common::telemetry::BuildDependentConfig(config_proto));
    config_.SetFlagForTest(kEmptyValue, ENABLE_SELLER_FRONTEND_BENCHMARKING);
    config_.SetFlagForTest(kEmptyValue, SELLER_ORIGIN_DOMAIN);
    config_.SetFlagForTest("0", GET_BID_RPC_TIMEOUT_MS);
    config_.SetFlagForTest("0", KEY_VALUE_SIGNALS_FETCH_RPC_TIMEOUT_MS);
    config_.SetFlagForTest("0", SCORE_ADS_RPC_TIMEOUT_MS);
    config_.SetFlagForTest("", CONSENTED_DEBUG_TOKEN);
    config_.SetFlagForTest(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetFlagForTest(kTrue, ENABLE_PROTECTED_AUDIENCE);
  }

  TrustedServersConfigClient config_ = TrustedServersConfigClient({});
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(SellerFrontEndServiceTest, ProtectedAuctionInputTypes);

TYPED_TEST(SellerFrontEndServiceTest, ReturnsInvalidInputOnEmptyCiphertext) {
  server_common::MockKeyFetcherManager key_fetcher_manager;
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};
  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_client_type(CLIENT_TYPE_ANDROID);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kEmptyProtectedAuctionCiphertextError);
}

TYPED_TEST(SellerFrontEndServiceTest, ReturnsInvalidArgumentOnKeyNotFound) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  auto protected_auction_input = MakeARandomProtectedAuctionInput<TypeParam>();
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillOnce(Return(std::nullopt));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(
      status.error_message(),
      absl::StrFormat(kErrorDecryptingCiphertextError,
                      absl::StrFormat(kMissingPrivateKey,
                                      absl::StrCat(HpkeKeyset{}.key_id))));
}

TYPED_TEST(SellerFrontEndServiceTest, ReturnsInvalidInputOnInvalidClientType) {
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  server_common::MockKeyFetcherManager key_fetcher_manager;
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  request.set_protected_auction_ciphertext("foo");
  request.set_client_type(CLIENT_TYPE_UNKNOWN);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_TRUE(
      absl::StrContains(status.error_message(), kUnsupportedClientType));
}

TYPED_TEST(SellerFrontEndServiceTest, ReturnsInvalidInputOnEmptyBuyerList) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  auto [protected_auction_input, request, encryption_context] =
      GetSampleSelectAdRequest<TypeParam>(CLIENT_TYPE_ANDROID,
                                          kSampleSellerDomain);
  request.mutable_auction_config()->clear_buyer_list();
  // This is a valid seller currency and should cause no issues.
  request.mutable_auction_config()->set_seller_currency("USD");
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kEmptyBuyerList);
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsInvalidInputOnInvalidSellerCurrency) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  auto [protected_auction_input, request, encryption_context] =
      GetSampleSelectAdRequest<TypeParam>(CLIENT_TYPE_ANDROID,
                                          kSampleSellerDomain);
  // This is not a valid currency code and should fail validation.
  request.mutable_auction_config()->set_seller_currency("dollars");
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kInvalidSellerCurrency);
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsInvalidInputOnInvalidBuyerCurrency) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  auto [protected_auction_input, request, encryption_context] =
      GetSampleSelectAdRequest<TypeParam>(CLIENT_TYPE_ANDROID,
                                          kSampleSellerDomain);
  SelectAdRequest::AuctionConfig::PerBuyerConfig per_buyer_config;
  per_buyer_config.set_buyer_signals(R"JSON({"someKey":["some","value"]})JSON");
  // This is not a valid currency code and should fail validation.
  per_buyer_config.set_buyer_currency("dollars");
  request.mutable_auction_config()->mutable_per_buyer_config()->insert(
      {std::string(kSampleBuyer), per_buyer_config});
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  ASSERT_EQ(status.error_message(), kInvalidBuyerCurrency);
}

TYPED_TEST(SellerFrontEndServiceTest, ErrorsOnMissingBuyerInputs) {
  // If ALL buyer inputs are missing, pending bids will decrease to 0
  // and we should abort the request. This test checks this behavior
  // assuming there is only a single buyer. If there are multiple buyers
  // and only a subset have missing inputs, the request should continue
  // with pending bids decreased by the amount of missing inputs.

  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  auto bfe_client = BuyerFrontEndAsyncClientFactoryMock();
  ClientRegistry clients{async_provider, scoring, bfe_client,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  SelectAdRequest request;
  TypeParam protected_auction_input;
  protected_auction_input.set_generation_id(kSampleGenerationId);
  protected_auction_input.set_publisher_name(kSamplePublisherName);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetProtoEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);
  request.mutable_auction_config()->mutable_buyer_list()->Add("Test");
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals("Test");
  request.mutable_auction_config()->set_auction_signals("Test");
  request.set_client_type(CLIENT_TYPE_ANDROID);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  AuctionResult auction_result = DecryptAppProtoAuctionResult(
      *response.mutable_auction_result_ciphertext(), encryption_context);
  ASSERT_FALSE(auction_result.is_chaff());
  EXPECT_EQ(auction_result.error().message(), kMissingBuyerInputs);
}

TYPED_TEST(SellerFrontEndServiceTest, SendsChaffOnMissingBuyerClient) {
  // If ALL buyer clients are missing, pending bids will decrease to 0
  // and we should mark the response as chaff. This test checks this behavior
  // assuming there is only a single buyer. If there are multiple buyers
  // and only a subset have missing clients, the request should continue
  // with pending bids decreased by the amount of missing clients.

  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  BuyerFrontEndAsyncClientFactoryMock client_factory_mock;
  EXPECT_CALL(client_factory_mock, Get)
      .WillOnce([](absl::string_view buyer_ig_owner) { return nullptr; });
  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto async_provider =
      MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>();
  auto scoring = ScoringAsyncClientMock();
  ClientRegistry clients{async_provider, scoring, client_factory_mock,
                         key_fetcher_manager, std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  grpc::ClientContext context;
  auto [protected_auction_input, request, encryption_context] =
      GetSampleSelectAdRequest<TypeParam>(CLIENT_TYPE_BROWSER,
                                          kSampleSellerDomain);
  SelectAdResponse response;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), encryption_context);
  ASSERT_TRUE(auction_result.is_chaff());
}

TYPED_TEST(SellerFrontEndServiceTest, SendsChaffOnEmptyGetBidsResponse) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

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
  TypeParam protected_auction_input;
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_auction_input.set_publisher_name(MakeARandomString());
  protected_auction_input.set_generation_id(kSampleGenerationId);
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext<TypeParam>(
          protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  EXPECT_EQ(request.auction_config().buyer_list_size(),
            protected_auction_input.buyer_input_size());
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    GetBidsResponse::GetBidsRawResponse response;
    SetupBuyerClientMock(local_buyer, buyer_clients, response,
                         /*repeated_get_allowed=*/true);
    expected_buyer_bids.try_emplace(
        local_buyer,
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signals provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  EXPECT_CALL(scoring_signals_provider, Get).Times(0);
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, Execute).Times(0);

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), encryption_context);
  ASSERT_TRUE(auction_result.is_chaff());
}

void SetupFailingBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients) {
  auto MockGetBids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
         const RequestMetadata& metadata, GetBidDoneCallback on_done,
         absl::Duration timeout) {
        return absl::InvalidArgumentError("Some Error");
      };
  auto SetupMockBuyer =
      [MockGetBids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(MockGetBids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [SetupMockBuyer](absl::string_view hostname) {
    return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_clients, Get(hostname)).WillOnce(MockBuyerFactoryCall);
}

void SetupBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients) {
  // Setup a buyer that returns a success.
  auto MockGetBids = [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                            get_values_request,
                        const RequestMetadata& metadata,
                        GetBidDoneCallback on_done, absl::Duration timeout) {
    ABSL_LOG(INFO) << "Getting mock bids returning mocked response to callback";
    std::move(on_done)(std::make_unique<GetBidsResponse::GetBidsRawResponse>());
    ABSL_LOG(INFO) << "Getting mock bids returned mocked response to callback";
    return absl::OkStatus();
  };
  auto SetupMockBuyer =
      [MockGetBids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(MockGetBids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [SetupMockBuyer](absl::string_view hostname) {
    return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_clients, Get(hostname)).WillOnce(MockBuyerFactoryCall);
}

TYPED_TEST(SellerFrontEndServiceTest, RawRequestFinishWithSuccess) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

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
  TypeParam protected_auction_input;
  protected_auction_input.set_generation_id(kSampleGenerationId);
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_auction_input.set_publisher_name(MakeARandomString());
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 1);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 1);
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request);
  const float bid_value = 1.0;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    std::string ad_url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid =
        BuildNewAdWithBid(ad_url, kSampleInterestGroupName, bid_value);
    GetBidsResponse::GetBidsRawResponse response;
    auto* mutable_bids = response.mutable_bids();
    mutable_bids->Add(std::move(bid));

    SetupBuyerClientMock(local_buyer, buyer_clients, response,
                         /*repeated_get_allowed=*/true);
    expected_buyer_bids.try_emplace(
        local_buyer,
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signals provider
  std::string ad_render_urls = "test scoring signals";
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
             const RequestMetadata& metadata, ScoreAdsDoneCallback on_done,
             absl::Duration timeout) {
            auto response =
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
            std::move(on_done)(std::move(response));
            return absl::OkStatus();
          });
  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
}

TYPED_TEST(SellerFrontEndServiceTest, ErrorsWhenCannotContactSellerKVServer) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

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
  TypeParam protected_auction_input;
  protected_auction_input.set_generation_id(kSampleGenerationId);
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_auction_input.set_publisher_name(MakeARandomString());
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 1);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 1);
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request);
  const float bid_value = 1.0;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    std::string ad_url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid =
        BuildNewAdWithBid(ad_url, kSampleInterestGroupName, bid_value);
    GetBidsResponse::GetBidsRawResponse response;
    auto* mutable_bids = response.mutable_bids();
    mutable_bids->Add(std::move(bid));

    SetupBuyerClientMock(local_buyer, buyer_clients, response,
                         /*repeated_get_allowed=*/true);
    expected_buyer_bids.try_emplace(
        local_buyer,
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signals provider
  absl::Status error_to_return(absl::StatusCode::kUnavailable,
                               "Could not reach Seller KV server.");
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           std::nullopt, false, error_to_return);
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
             const RequestMetadata& metadata, ScoreAdsDoneCallback on_done,
             absl::Duration timeout) {
            auto response =
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
            std::move(on_done)(std::move(response));
            return absl::OkStatus();
          });
  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_FALSE(status.ok()) << server_common::ToAbslStatus(status);
  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TYPED_TEST(SellerFrontEndServiceTest,
           BuyerClientFailsWithCorrectOverallStatus) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->Add()->set_name(
      kSampleInterestGroupName);
  DecodedBuyerInputs decoded_buyer_inputs;
  decoded_buyer_inputs.emplace(kSampleBuyer, std::move(buyer_input));
  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_inputs =
      GetEncodedBuyerInputMap(decoded_buyer_inputs);
  EXPECT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();

  // Setup a valid SelectAdRequest with the aforementioned buyer input.
  SelectAdRequest request;
  TypeParam protected_auction_input;
  protected_auction_input.set_generation_id(kSampleGenerationId);
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);
  request.mutable_auction_config()->set_seller(kSampleSellerDomain);
  request.mutable_auction_config()->set_seller_signals(kSampleSellerSignals);
  request.mutable_auction_config()->set_auction_signals(kSampleAuctionSignals);
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_auction_input.set_publisher_name(MakeARandomString());
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client that throws an error.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 1);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 1);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    SetupFailingBuyerClientMock(local_buyer, buyer_clients);
  }

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, Execute).Times(0);
  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_FALSE(status.ok()) << server_common::ToAbslStatus(status);
  ASSERT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
  ASSERT_EQ(status.error_message(), kInternalServerError);
}

TYPED_TEST(SellerFrontEndServiceTest, AnyBuyerNotErroringMeansOverallSuccess) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  // Setup a valid SelectAdRequest with the aforementioned buyer inputs.
  auto protected_auction_input = MakeARandomProtectedAuctionInput<TypeParam>();
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client that throws an error.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);
  auto i = 0;
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    if (i > 0) {
      SetupBuyerClientMock(local_buyer, buyer_clients);
    } else {
      SetupFailingBuyerClientMock(local_buyer, buyer_clients);
    }
    i++;
  }

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, Execute).Times(0);
  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  EXPECT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
}

TYPED_TEST(SellerFrontEndServiceTest,
           OneBogusAndOneLegitBuyerWaitsForAllBuyerBids) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  // Setup a valid SelectAdRequest with the aforementioned buyer inputs.
  TypeParam protected_auction_input =
      MakeARandomProtectedAuctionInput<TypeParam>();
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client that throws an error.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);
  auto i = 0;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] : protected_auction_input.buyer_input()) {
    if (i == 1) {
      // Setup valid client for only the last of the buyer since we want to
      // cover the scenario where select ad reactor may be terminating the
      // SelectAdRequest RPC prematurely on finding a a buyer with no client.
      AdWithBid bid = BuildNewAdWithBid(
          /*ad_url=*/absl::StrCat(buyer, "/ad"), kSampleInterestGroupName,
          kNonZeroBidValue);
      GetBidsResponse::GetBidsRawResponse get_bids_response;
      get_bids_response.mutable_bids()->Add(std::move(bid));
      SetupBuyerClientMock(buyer, buyer_clients, get_bids_response);
      expected_buyer_bids.try_emplace(
          buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                     get_bids_response));
    } else {
      EXPECT_CALL(buyer_clients, Get(buyer)).WillOnce(Return(nullptr));
    }
    ++i;
  }

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  std::optional<std::string> ad_render_urls{kSampleAdRenderUrl};
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);

  ScoringAsyncClientMock scoring_client;
  absl::BlockingCounter scoring_done(1);
  ScoreAdsResponse::AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([&scoring_done, &winner](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    const RequestMetadata& metadata,
                    ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        ScoreAdsResponse::ScoreAdsRawResponse response;
        float i = 1;
        ErrorAccumulator error_accumulator;
        // Last bid wins.
        for (const auto& bid : score_ads_request->ad_bids()) {
          ScoreAdsResponse::AdScore score;
          EXPECT_FALSE(bid.render().empty());
          score.set_render(bid.render());
          score.mutable_component_renders()->CopyFrom(bid.ad_components());
          EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
          score.set_desirability(i++);
          score.set_buyer_bid(i);
          score.set_interest_group_name(bid.interest_group_name());
          *response.mutable_ad_score() = score;
          winner = score;
        }
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(response));
        scoring_done.DecrementCount();
        return absl::OkStatus();
      });

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);
  scoring_done.Wait();

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), encryption_context);
  ABSL_LOG(INFO) << "Response: " << auction_result.DebugString();
  EXPECT_FALSE(auction_result.is_chaff());
}

TYPED_TEST(SellerFrontEndServiceTest, SkipsBuyerCallsAfterLimit) {
  const int num_buyers = 3;

  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  // Setup a valid SelectAdRequest with the aforementioned buyer inputs.
  TypeParam protected_auction_input =
      MakeARandomProtectedAuctionInput<TypeParam>(/*num_buyers=*/num_buyers);
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client that throws an error.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);
  BuyerBidsResponseMap expected_buyer_bids;
  int buyer_calls_counter = 0;
  for (const auto& [buyer, unused] : protected_auction_input.buyer_input()) {
    AdWithBid bid =
        BuildNewAdWithBid(/*ad_url=*/absl::StrCat(buyer, "/ad"),
                          kSampleInterestGroupName, kNonZeroBidValue);
    GetBidsResponse::GetBidsRawResponse get_bids_response;
    get_bids_response.mutable_bids()->Add(std::move(bid));
    SetupBuyerClientMock(buyer, buyer_clients, get_bids_response,
                         /*repeated_get_allowed=*/false,
                         /*each_call_required=*/false,
                         /*buyer_calls_counter=*/&buyer_calls_counter);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bids_response));
  }

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  std::optional<std::string> ad_render_urls{kSampleAdRenderUrl};
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls, /*repeated_get_allowed=*/false,
                           /*server_error_to_return=*/std::nullopt,
                           /*expected_num_bids=*/2);

  ScoringAsyncClientMock scoring_client;
  absl::Notification notification;
  ScoreAdsResponse::AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([&notification, &winner](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    const RequestMetadata& metadata,
                    ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        ScoreAdsResponse::ScoreAdsRawResponse response;
        float i = 1;
        ErrorAccumulator error_accumulator;
        // Last bid wins.
        for (const auto& bid : score_ads_request->ad_bids()) {
          ScoreAdsResponse::AdScore score;
          EXPECT_FALSE(bid.render().empty());
          score.set_render(bid.render());
          score.mutable_component_renders()->CopyFrom(bid.ad_components());
          EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
          score.set_desirability(i++);
          score.set_buyer_bid(i);
          score.set_interest_group_name(bid.interest_group_name());
          *response.mutable_ad_score() = score;
          winner = score;
        }
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(response));
        notification.Notify();
        return absl::OkStatus();
      });

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);
  notification.WaitForNotification();

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), encryption_context);
  ABSL_LOG(INFO) << "Response: " << auction_result.DebugString();
  EXPECT_FALSE(auction_result.is_chaff());

  EXPECT_EQ(buyer_calls_counter, 2);
}

TYPED_TEST(SellerFrontEndServiceTest, InternalErrorsFromScoringCauseAChaff) {
  const int num_buyers = 1;

  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  // Setup a valid SelectAdRequest with the aforementioned buyer inputs.
  TypeParam protected_auction_input =
      MakeARandomProtectedAuctionInput<TypeParam>(/*num_buyers=*/1);
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] : protected_auction_input.buyer_input()) {
    AdWithBid bid =
        BuildNewAdWithBid(/*ad_url=*/absl::StrCat(buyer, "/ad"),
                          kSampleInterestGroupName, kNonZeroBidValue);
    GetBidsResponse::GetBidsRawResponse get_bids_response;
    get_bids_response.mutable_bids()->Add(std::move(bid));
    SetupBuyerClientMock(buyer, buyer_clients, get_bids_response,
                         /*repeated_get_allowed=*/false,
                         /*each_call_required=*/false);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bids_response));
  }

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  std::optional<std::string> ad_render_urls{kSampleAdRenderUrl};
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls, /*repeated_get_allowed=*/false);

  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                       score_ads_request,
                   const RequestMetadata& metadata,
                   ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        std::move(on_done)(absl::InternalError(""));
        return absl::OkStatus();
      });

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_TRUE(status.ok()) << server_common::ToAbslStatus(status);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), encryption_context);
  ABSL_LOG(INFO) << "Response: " << auction_result.DebugString();
  EXPECT_TRUE(auction_result.is_chaff());
}

TYPED_TEST(SellerFrontEndServiceTest,
           NonInternalErrorsFromScoringSentToAdService) {
  const int num_buyers = 1;

  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  // Setup a valid SelectAdRequest with the aforementioned buyer inputs.
  TypeParam protected_auction_input =
      MakeARandomProtectedAuctionInput<TypeParam>(/*num_buyers=*/1);
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_BROWSER);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);
  int buyer_input_count = protected_auction_input.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] : protected_auction_input.buyer_input()) {
    AdWithBid bid =
        BuildNewAdWithBid(/*ad_url=*/absl::StrCat(buyer, "/ad"),
                          kSampleInterestGroupName, kNonZeroBidValue);
    GetBidsResponse::GetBidsRawResponse get_bids_response;
    get_bids_response.mutable_bids()->Add(std::move(bid));
    SetupBuyerClientMock(buyer, buyer_clients, get_bids_response,
                         /*repeated_get_allowed=*/false,
                         /*each_call_required=*/false);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bids_response));
  }

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  std::optional<std::string> ad_render_urls{kSampleAdRenderUrl};
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls, /*repeated_get_allowed=*/false);

  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                       score_ads_request,
                   const RequestMetadata& metadata,
                   ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        std::move(on_done)(
            absl::ResourceExhaustedError(kErrorIntendedForAdServer));
        return absl::OkStatus();
      });

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_FALSE(status.ok()) << server_common::ToAbslStatus(status);
  ASSERT_EQ(status.error_code(), grpc::RESOURCE_EXHAUSTED);
  ASSERT_THAT(status.error_message(), HasSubstr(kErrorIntendedForAdServer));
}

TYPED_TEST(SellerFrontEndServiceTest, ReturnsErrorForAndroidComponentAuction) {
  this->config_.SetFlagForTest(kSampleSellerDomain, SELLER_ORIGIN_DOMAIN);

  // Setup a valid SelectAdRequest with the aforementioned buyer inputs.
  TypeParam protected_auction_input =
      MakeARandomProtectedAuctionInput<TypeParam>(/*num_buyers=*/1);
  SelectAdRequest request =
      MakeARandomSelectAdRequest(kSampleSellerDomain, protected_auction_input);
  request.set_client_type(CLIENT_TYPE_ANDROID);
  request.mutable_auction_config()->set_top_level_seller("foo");
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);

  // Setup buyer client.
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;

  // Scoring signals provider mock.
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;

  server_common::MockKeyFetcherManager key_fetcher_manager;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager,
                         std::move(async_reporter)};

  SellerFrontEndService seller_frontend_service(&this->config_,
                                                std::move(clients));
  auto start_sfe_result = StartLocalService(&seller_frontend_service);
  auto stub = CreateServiceStub<SellerFrontEnd>(start_sfe_result.port);

  SelectAdResponse response;
  grpc::ClientContext context;
  grpc::Status status = stub->SelectAd(&context, request, &response);

  ASSERT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
  EXPECT_TRUE(absl::StrContains(status.error_message(),
                                kDeviceComponentAuctionWithAndroid));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
