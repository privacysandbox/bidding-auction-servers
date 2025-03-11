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
#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/util/buyer_frontend_test_utils.h"
#include "services/buyer_frontend_service/util/proto_factory.h"
#include "services/common/chaffing/transcoding_utils.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/proto_utils.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/test/utils/test_utils.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
namespace privacy_sandbox::bidding_auction_servers {
namespace {

using google::protobuf::TextFormat;

constexpr absl::string_view kSampleBuyerDebugId = "sample-buyer-debug-id";
constexpr absl::string_view kSampleGenerationId = "sample-seller-debug-id";

using ::google::protobuf::util::MessageDifferencer;
using ::testing::_;
using ::testing::AllOf;
using ::testing::An;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using GenerateProtectedAppSignalsBidsRawRequest =
    GenerateProtectedAppSignalsBidsRequest::
        GenerateProtectedAppSignalsBidsRawRequest;
using GenerateProtectedAppSignalsBidsRawResponse =
    GenerateProtectedAppSignalsBidsResponse::
        GenerateProtectedAppSignalsBidsRawResponse;
using server_common::ConsentedDebugConfiguration;

constexpr absl::Duration kTestProtectedAppSignalsGenerateBidTimeout =
    absl::Milliseconds(1);
constexpr char kTestInterestGroupName[] = "test_ig";
constexpr int kTestBidValue1 = 10.0;
constexpr int kTestAdCost1 = 2.0;
constexpr int kTestModelingSignals1 = 54;
constexpr char kTestRender1[] = "https://test-render.com";
constexpr char kTestMetadataKey1[] = "test_metadata_key";
constexpr char kTestAdComponent[] = "test_ad_component";
constexpr char kTestCurrency1[] = "USD";
constexpr int kTestMetadataValue1 = 12;

constexpr int kTestBidValue2 = 20.0;
constexpr int kTestAdCost2 = 4.0;
constexpr char kTestRender2[] = "https://test-render-2.com";
constexpr char kTestCurrency2[] = "RS";
constexpr char kBiddingSignalsToBeReturned[] =
    R"JSON({
    "keys": {
        "key":[123,456]
    },
    "perInterestGroupData": {
        "test_ig": {
            "updateIfOlderThanMs": 123
        }
    }
})JSON";
constexpr char kEmptyBiddingSignals[] = R"JSON({})JSON";

constexpr char kCompressionGroup[] = R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "key": {
              "value": "[123,456]"
            }
          }
        }
      ]
    }
  ])JSON";

constexpr char kCompressionGroupWrapper[] =
    R"(
    compression_groups {
      compression_group_id : 33
      content : "%s"
  })";

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
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
  EXPECT_CALL(crypto_client, HpkeDecrypt)
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
  EXPECT_CALL(crypto_client, AeadEncrypt)
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
}

class GetBidUnaryReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<GetBidsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);
    get_bids_config_.is_protected_app_signals_enabled = false;
    get_bids_config_.is_protected_audience_enabled = true;
    get_bids_config_.is_tkv_v2_browser_enabled = false;
    get_bids_config_.test_mode = true;
    TrustedServersConfigClient config_client({});
    config_client.SetOverride(kTrue, TEST_MODE);
    config_client.SetOverride(kFalse, CONSENT_ALL_REQUESTS);
    key_fetcher_manager_ =
        CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
    SetupMockCryptoClientWrapper(*crypto_client_);

    raw_request_ = MakeARandomGetBidsRawRequest();
    auto interest_group = raw_request_.mutable_buyer_input_for_bidding()
                              ->mutable_interest_groups()
                              ->Add();
    interest_group->set_name(kTestInterestGroupName);
    interest_group->add_bidding_signals_keys("key");
    request_.set_request_ciphertext(raw_request_.SerializeAsString());
    request_.set_key_id(MakeARandomString());
  }

  grpc::CallbackServerContext context_;
  GetBidsRequest request_ = MakeARandomGetBidsRequest();
  GetBidsRequest::GetBidsRawRequest raw_request_;
  GetBidsResponse response_;
  BiddingAsyncClientMock bidding_client_mock_;
  MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>
      bidding_signals_provider_;
  GetBidsConfig get_bids_config_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<KVAsyncClientMock> kv_async_client_ =
      std::make_unique<KVAsyncClientMock>();
  MockExecutor executor_;
};

TEST_F(GetBidUnaryReactorTest, LoadsBiddingSignalsAndCallsBiddingServer) {
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned});

  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);

  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, LoadsBiddingSignalsAndCallsBiddingServerV2) {
  get_bids_config_.is_tkv_v2_browser_enabled = true;
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  SetupBiddingProviderMockV2(kv_async_client_.get(), response);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServerClientTypeAndroidCallsKvV2) {
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  SetupBiddingProviderMockV2(kv_async_client_.get(), response);
  absl::Notification notification;
  EXPECT_CALL(
      bidding_client_mock_,
      ExecuteInternal(
          An<std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateBidsResponse::GenerateBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });
  raw_request_.set_client_type(CLIENT_TYPE_ANDROID);
  request_.set_request_ciphertext(raw_request_.SerializeAsString());
  request_.set_key_id(MakeARandomString());

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, LoadsBiddingSignalsAndCallsBiddingServerHybrid) {
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {
          .bidding_signals_value = kBiddingSignalsToBeReturned,
      });

  get_bids_config_.is_tkv_v2_browser_enabled = false;
  get_bids_config_.is_hybrid_enabled = true;
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  SetupBiddingProviderMockHybrid(kv_async_client_.get(), response,
                                 kBiddingSignalsToBeReturned);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /* response_metadata= */ {});
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServerHybridAndroid) {
  get_bids_config_.is_tkv_v2_browser_enabled = true;
  get_bids_config_.is_hybrid_enabled = true;
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  // for Android we never call v1, even if hybrid is enabled
  EXPECT_CALL(bidding_signals_provider_, Get).Times(0);
  SetupBiddingProviderMockV2(kv_async_client_.get(), response);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /* response_metadata= */ {});
        notification.Notify();
        return absl::OkStatus();
      });
  raw_request_.set_client_type(CLIENT_TYPE_ANDROID);
  request_.set_request_ciphertext(raw_request_.SerializeAsString());
  request_.set_key_id(MakeARandomString());

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServerHybridV1ShortCircuit) {
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned,
       .is_hybrid_v1_return = true});
  get_bids_config_.is_tkv_v2_browser_enabled = false;
  get_bids_config_.is_hybrid_enabled = true;
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /* response_metadata= */ {});
        notification.Notify();
        return absl::OkStatus();
      });
  // v1 indicated that no need to call v2 via a header
  EXPECT_CALL(*kv_async_client_, ExecuteInternal).Times(0);
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest,
       AddsUpdateInterestGroupListToGetBidsRawResponse) {
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned});

  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();

  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  ASSERT_FALSE(
      raw_response.update_interest_group_list().interest_groups().empty());
  EXPECT_EQ(
      raw_response.update_interest_group_list().interest_groups()[0].index(),
      0);
  EXPECT_EQ(raw_response.update_interest_group_list()
                .interest_groups()[0]
                .update_if_older_than_ms(),
            123);
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServer_EncryptionEnabled) {
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned});

  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        auto raw_response =
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>();
        raw_response->mutable_bids()->Add();
        std::move(on_done)(std::move(raw_response),
                           /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();

  EXPECT_FALSE(response_.response_ciphertext().empty());
}

TEST_F(GetBidUnaryReactorTest,
       LoadsBiddingSignalsAndCallsBiddingServer_EncryptionEnabledV2) {
  get_bids_config_.is_tkv_v2_browser_enabled = true;
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  SetupBiddingProviderMockV2(kv_async_client_.get(), response);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        auto raw_response =
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>();
        raw_response->mutable_bids()->Add();
        std::move(on_done)(std::move(raw_response),
                           /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  // Wait for reactor to set response_.
  notification.WaitForNotification();

  EXPECT_FALSE(response_.response_ciphertext().empty());
}

auto EqLogContext(const server_common::LogContext& log_context) {
  return AllOf(Property(&server_common::LogContext::generation_id,
                        Eq(log_context.generation_id())),
               Property(&server_common::LogContext::adtech_debug_id,
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
  request_.set_request_ciphertext(raw_request_.SerializeAsString());

  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned});

  GenerateBidsRequest::GenerateBidsRawRequest
      expected_generate_bids_raw_request;
  auto* expected_log_context =
      expected_generate_bids_raw_request.mutable_log_context();
  expected_log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  expected_log_context->set_generation_id(kSampleGenerationId);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification, &expected_generate_bids_raw_request](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_THAT(generate_bids_raw_request,
                    Pointee(EqGenerateBidsRawRequestWithLogContext(
                        expected_generate_bids_raw_request)));
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });
  GetBidsUnaryReactor get_bids_unary_reactor(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  get_bids_unary_reactor.Execute();
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, VerifyLogContextPropagatesV2) {
  auto* log_context = raw_request_.mutable_log_context();
  log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  log_context->set_generation_id(kSampleGenerationId);
  request_.set_request_ciphertext(raw_request_.SerializeAsString());

  get_bids_config_.is_tkv_v2_browser_enabled = true;
  kv_server::v2::GetValuesResponse response;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::StrFormat(kCompressionGroupWrapper,
                      absl::CEscape(RemoveWhiteSpaces(kCompressionGroup))),
      &response));
  SetupBiddingProviderMockV2(kv_async_client_.get(), response);

  GenerateBidsRequest::GenerateBidsRawRequest
      expected_generate_bids_raw_request;
  auto* expected_log_context =
      expected_generate_bids_raw_request.mutable_log_context();
  expected_log_context->set_adtech_debug_id(kSampleBuyerDebugId);
  expected_log_context->set_generation_id(kSampleGenerationId);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification, &expected_generate_bids_raw_request](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_THAT(generate_bids_raw_request,
                    Pointee(EqGenerateBidsRawRequestWithLogContext(
                        expected_generate_bids_raw_request)));
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });
  GetBidsUnaryReactor get_bids_unary_reactor(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  get_bids_unary_reactor.Execute();
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, HandleChaffRequest) {
  get_bids_config_.is_chaffing_enabled = true;

  EXPECT_CALL(bidding_client_mock_, ExecuteInternal).Times(0);

  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(true);
  raw_request.mutable_log_context()->set_generation_id(kTestGenerationId);
  GetBidsRequest request;
  request.set_request_ciphertext(*EncodeAndCompressGetBidsPayload(
      raw_request, CompressionType::kGzip, 999));
  request.set_key_id(MakeARandomString());

  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  metric::MetricContextMap<GetBidsRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto))
      ->Get(&request);

  EXPECT_CALL(executor_, RunAfter)
      .Times(1)
      .WillOnce([](const absl::Duration duration,
                   absl::AnyInvocable<void()> closure) {
        closure();
        server_common::TaskId id;
        return id;
      });
  GetBidsUnaryReactor class_under_test(
      context_, request, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();

  ASSERT_FALSE(response_.response_ciphertext().empty());
  absl::StatusOr<DecodedGetBidsPayload<GetBidsResponse::GetBidsRawResponse>>
      decoded_payload =
          DecodeGetBidsPayload<GetBidsResponse::GetBidsRawResponse>(
              response_.response_ciphertext());

  ASSERT_TRUE(decoded_payload.ok());
  // For now, we don't support any version/compression bytes besides 0.
  EXPECT_EQ(decoded_payload->version, 0);
  EXPECT_EQ(decoded_payload->compression_type, CompressionType::kGzip);
  // Empty proto is sent back; the payload should be all padding.
  EXPECT_EQ(decoded_payload->get_bids_proto.ByteSizeLong(), 0);
  // Verify the response has the expected padding.
  EXPECT_GT(response_.response_ciphertext().length(),
            kMinChaffResponseSizeBytes);
}

TEST_F(GetBidUnaryReactorTest, HandleChaffRequestV2) {
  get_bids_config_.is_chaffing_enabled = true;
  get_bids_config_.is_tkv_v2_browser_enabled = true;

  EXPECT_CALL(bidding_client_mock_, ExecuteInternal).Times(0);

  GetBidsRequest::GetBidsRawRequest raw_request;
  raw_request.set_is_chaff(true);
  raw_request.mutable_log_context()->set_generation_id(kTestGenerationId);
  GetBidsRequest request;
  request.set_request_ciphertext(*EncodeAndCompressGetBidsPayload(
      raw_request, CompressionType::kGzip, 999));
  request.set_key_id(MakeARandomString());

  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  metric::MetricContextMap<GetBidsRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto))
      ->Get(&request);

  EXPECT_CALL(executor_, RunAfter)
      .Times(1)
      .WillOnce([](const absl::Duration duration,
                   absl::AnyInvocable<void()> closure) {
        closure();
        server_common::TaskId id;
        return id;
      });
  GetBidsUnaryReactor class_under_test(
      context_, request, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();

  ASSERT_FALSE(response_.response_ciphertext().empty());
  absl::StatusOr<DecodedGetBidsPayload<GetBidsResponse::GetBidsRawResponse>>
      decoded_payload =
          DecodeGetBidsPayload<GetBidsResponse::GetBidsRawResponse>(
              response_.response_ciphertext());

  ASSERT_TRUE(decoded_payload.ok());
  // For now, we don't support any version/compression bytes besides 0.
  EXPECT_EQ(decoded_payload->version, 0);
  EXPECT_EQ(decoded_payload->compression_type, CompressionType::kGzip);
  // Empty proto is sent back; the payload should be all padding.
  EXPECT_EQ(decoded_payload->get_bids_proto.ByteSizeLong(), 0);
  // Verify the response has the expected padding.
  EXPECT_GT(response_.response_ciphertext().length(),
            kMinChaffResponseSizeBytes);
}

TEST_F(GetBidUnaryReactorTest, ValidatePriorityVectorFiltering) {
  constexpr char bidding_signals[] =
      R"JSON({
    "keys": {
        "key":[123,456]
    },
    "perInterestGroupData": {
        "test_ig": {
            "updateIfOlderThanMs": 123,
            "priorityVector": {
              "entry": -1
            }
        },
        "test_ig_2": {
            "updateIfOlderThanMs": 123,
            "priorityVector": {
              "entry": 1
            }
        }
    }
})JSON";

  auto interest_group = raw_request_.mutable_buyer_input_for_bidding()
                            ->mutable_interest_groups()
                            ->Add();
  interest_group->set_name("test_ig_2");
  interest_group->add_bidding_signals_keys("key");

  raw_request_.set_priority_signals(R"JSON({"entry": 1.0})JSON");
  request_.set_request_ciphertext(raw_request_.SerializeAsString());

  SetupBiddingProviderMock(bidding_signals_provider_,
                           {.bidding_signals_value = bidding_signals});

  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        // 'test_ig_2's is the only IG with a priority greater than zero, so it
        // should not have been filtered out.
        EXPECT_EQ(generate_bids_raw_request->interest_group_for_bidding_size(),
                  1);
        EXPECT_EQ(
            generate_bids_raw_request->interest_group_for_bidding(0).name(),
            "test_ig_2");

        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  get_bids_config_.priority_vector_enabled = true;
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);

  class_under_test.Execute();
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, ValidatePriorityVectorFilteringV2) {
  get_bids_config_.is_tkv_v2_browser_enabled = true;
  std::string compression_group =
      R"JSON(
  [
    {
      "id": 0,
      "keyGroupOutputs": [
        {
          "tags": [
            "keys"
          ],
          "keyValues": {
            "key": {
              "value": "[123,456]"
            }
          }
        },
        {
          "tags": [
            "interestGroupNames"
          ],
          "keyValues": {
            "test_ig": {
              "value": "{\"priorityVector\":{\"entry\":-1}, \"updateIfOlderThanMs\": 123}"
            }
          }
        }
      ]
    },
    {
      "id": 1,
      "keyGroupOutputs": [
        {
          "tags": [
            "interestGroupNames"
          ],
          "keyValues": {
            "test_ig_2": {
              "value": "{\"priorityVector\":{\"entry\":1}, \"updateIfOlderThanMs\": 123}"
            }
          }
        }
      ]
    }
  ])JSON";
  kv_server::v2::GetValuesResponse response =
      ParseTextOrDie<kv_server::v2::GetValuesResponse>(
          absl::StrFormat(kCompressionGroupWrapper,
                          absl::CEscape(RemoveWhiteSpaces(compression_group))));
  SetupBiddingProviderMockV2(kv_async_client_.get(), response);

  *raw_request_.mutable_buyer_input()->mutable_interest_groups()->Add() =
      BuyerInput_InterestGroupBuilder()
          .SetName("test_ig_2")
          .AddBiddingSignalsKeys("key");
  raw_request_.set_priority_signals(R"JSON({"entry": 1.0})JSON");
  request_.set_request_ciphertext(raw_request_.SerializeAsString());

  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        // 'test_ig_2's is the only IG with a priority greater than zero, so it
        // should not have been filtered out.
        EXPECT_EQ(generate_bids_raw_request->interest_group_for_bidding_size(),
                  1);
        EXPECT_EQ(
            generate_bids_raw_request->interest_group_for_bidding(0).name(),
            "test_ig_2");

        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });
  get_bids_config_.priority_vector_enabled = true;
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, DoesNotCallBiddingForEmptyBiddingSignals) {
  SetupBiddingProviderMock(bidding_signals_provider_,
                           {.bidding_signals_value = kEmptyBiddingSignals});

  // Bidding is not called because bidding signals are empty but they are
  // required by default (bidding_signals_fetch_mode is REQUIRED).
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal).Times(0);

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);

  class_under_test.Execute();
}

TEST_F(GetBidUnaryReactorTest,
       CallsBiddingForEmptyBiddingSignalsWhenSignalsFetchedButOptional) {
  // Setting flag to fetch bidding signals but make them optional.
  get_bids_config_.bidding_signals_fetch_mode =
      BiddingSignalsFetchMode::FETCHED_BUT_OPTIONAL;

  SetupBiddingProviderMock(bidding_signals_provider_,
                           {.bidding_signals_value = kEmptyBiddingSignals});

  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(generate_bids_raw_request->interest_group_for_bidding_size(),
                  1);
        auto ig_for_bidding =
            generate_bids_raw_request->interest_group_for_bidding(0);
        EXPECT_EQ(ig_for_bidding.name(), kTestInterestGroupName);
        // We expect interest groups with null trusted bidding signals to not be
        // filtered because bidding signals have been made optional.
        EXPECT_EQ(ig_for_bidding.trusted_bidding_signals(),
                  kNullBiddingSignalsJson);

        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);

  class_under_test.Execute();
  notification.WaitForNotification();
}

TEST_F(GetBidUnaryReactorTest, SkipsKVLookupWhenSignalsFetchModeNotFetched) {
  // Setting flag to skip the call to KV altogether.
  get_bids_config_.bidding_signals_fetch_mode =
      BiddingSignalsFetchMode::NOT_FETCHED;

  // We expect that the bidding client will still in fact be called, despite no
  // call to the bidding signals provider and therefore no signals.
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(generate_bids_raw_request->interest_group_for_bidding_size(),
                  1);
        auto ig_for_bidding =
            generate_bids_raw_request->interest_group_for_bidding(0);
        EXPECT_EQ(ig_for_bidding.name(), kTestInterestGroupName);
        EXPECT_EQ(ig_for_bidding.trusted_bidding_signals_keys_size(), 0);
        EXPECT_EQ(ig_for_bidding.trusted_bidding_signals(),
                  kNullBiddingSignalsJson);

        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });

  // bidding_signals_async_provider and kv_async_client will only be nullptr
  // when bidding_signals_fetch_mode is NOT_FETCHED, so there is no need for a
  // separate test for these.
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, /*bidding_signals_async_provider=*/nullptr,
      bidding_client_mock_, get_bids_config_, key_fetcher_manager_.get(),
      crypto_client_.get(), /*kv_async_client=*/nullptr, executor_);

  class_under_test.Execute();
  notification.WaitForNotification();
}

class GetProtectedAppSignalsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<GetBidsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);

    get_bids_config_.is_protected_app_signals_enabled = true;
    get_bids_config_.is_protected_audience_enabled = true;
    get_bids_config_.is_tkv_v2_browser_enabled = false;

    TrustedServersConfigClient config_client({});
    config_client.SetOverride(kTrue, TEST_MODE);
    config_client.SetOverride(kFalse, CONSENT_ALL_REQUESTS);
    key_fetcher_manager_ =
        CreateKeyFetcherManager(config_client,
                                /*public_key_fetcher=*/nullptr);
    SetupMockCryptoClientWrapper(*crypto_client_);

    server_common::log::SetGlobalPSVLogLevel(10);
  }

  grpc::CallbackServerContext context_;
  GetBidsRequest request_;
  GetBidsResponse response_;
  BiddingAsyncClientMock bidding_client_mock_;
  ProtectedAppSignalsBiddingAsyncClientMock
      protected_app_signals_bidding_client_mock_;
  MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>
      bidding_signals_provider_;
  GetBidsConfig get_bids_config_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<KVAsyncClientMock> kv_async_client_ =
      std::make_unique<KVAsyncClientMock>();
  MockExecutor executor_;
};

auto EqProtectedAppSignals(const ProtectedAppSignals& expected) {
  return AllOf(Property(&ProtectedAppSignals::app_install_signals,
                        Eq(expected.app_install_signals())),
               Property(&ProtectedAppSignals::encoding_version,
                        Eq(expected.encoding_version())));
}

auto EqConsentedDebugConfig(const ConsentedDebugConfiguration& expected) {
  return AllOf(Property(&ConsentedDebugConfiguration::token, expected.token()),
               Property(&ConsentedDebugConfiguration::is_consented,
                        expected.is_consented()));
}

auto EqGenerateProtectedAppSignalsBidsRawRequest(
    const GenerateProtectedAppSignalsBidsRawRequest& expected) {
  return AllOf(
      Property(&GenerateProtectedAppSignalsBidsRawRequest::auction_signals,
               Eq(expected.auction_signals())),
      Property(&GenerateProtectedAppSignalsBidsRawRequest::buyer_signals,
               Eq(expected.buyer_signals())),
      Property(
          &GenerateProtectedAppSignalsBidsRawRequest::protected_app_signals,
          EqProtectedAppSignals(expected.protected_app_signals())),
      Property(&GenerateProtectedAppSignalsBidsRawRequest::seller,
               Eq(expected.seller())),
      Property(&GenerateProtectedAppSignalsBidsRawRequest::publisher_name,
               Eq(expected.publisher_name())),
      Property(
          &GenerateProtectedAppSignalsBidsRawRequest::enable_debug_reporting,
          Eq(expected.enable_debug_reporting())),
      Property(&GenerateProtectedAppSignalsBidsRawRequest::log_context,
               EqLogContext(expected.log_context())),
      Property(
          &GenerateProtectedAppSignalsBidsRawRequest::consented_debug_config,
          EqConsentedDebugConfig(expected.consented_debug_config())));
}

TEST_F(GetProtectedAppSignalsTest, CorrectGenerateBidSentToBiddingService) {
  request_ = CreateGetBidsRequest();

  // Expected generate bid request to be sent to the bidding service.
  GenerateProtectedAppSignalsBidsRawRequest expected_request;
  expected_request.set_auction_signals(kTestAuctionSignals);
  expected_request.set_buyer_signals(kTestBuyerSignals);
  expected_request.mutable_protected_app_signals()->set_encoding_version(
      kTestEncodingVersion);
  expected_request.mutable_protected_app_signals()->set_app_install_signals(
      kTestProtectedAppSignals);
  expected_request.set_seller(kTestSeller);
  expected_request.set_publisher_name(kTestPublisherName);
  expected_request.set_enable_debug_reporting(true);
  expected_request.mutable_log_context()->set_generation_id(kTestGenerationId);
  expected_request.mutable_log_context()->set_adtech_debug_id(
      kTestAdTechDebugId);
  expected_request.mutable_consented_debug_config()->set_is_consented(true);
  expected_request.mutable_consented_debug_config()->set_token(
      kTestConsentedDebuggingToken);

  EXPECT_CALL(
      protected_app_signals_bidding_client_mock_,
      ExecuteInternal(Pointee(EqGenerateProtectedAppSignalsBidsRawRequest(
                          expected_request)),
                      An<grpc::ClientContext*>(),
                      An<absl::AnyInvocable<
                          void(absl::StatusOr<std::unique_ptr<
                                   GenerateProtectedAppSignalsBidsRawResponse>>,
                               ResponseMetadata) &&>>(),
                      An<absl::Duration>(), An<RequestConfig>()))
      .Times(1);

  // No protected audience buyer input and hence no outbound call to bidding
  // service.
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal).Times(0);

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_,
      &protected_app_signals_bidding_client_mock_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
}

TEST_F(GetProtectedAppSignalsTest, TimeoutIsRespected) {
  request_ = CreateGetBidsRequest();

  get_bids_config_.protected_app_signals_generate_bid_timeout_ms =
      ToDoubleMilliseconds(kTestProtectedAppSignalsGenerateBidTimeout);
  EXPECT_CALL(
      protected_app_signals_bidding_client_mock_,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          Eq(kTestProtectedAppSignalsGenerateBidTimeout), An<RequestConfig>()));

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_,
      &protected_app_signals_bidding_client_mock_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
}

TEST_F(GetProtectedAppSignalsTest, RespectsFeatureFlagOff) {
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned});
  get_bids_config_.is_protected_app_signals_enabled = false;
  EXPECT_CALL(
      protected_app_signals_bidding_client_mock_,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .Times(0);
  absl::Notification notification;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&notification](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        generate_bids_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>(),
            /*response_metadata=*/{});
        notification.Notify();
        return absl::OkStatus();
      });
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_,
      &protected_app_signals_bidding_client_mock_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  notification.WaitForNotification();
}

TEST_F(GetProtectedAppSignalsTest, RespectsProtectedAudienceFeatureFlagOff) {
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned,
       .match_any_params_any_times = true});
  get_bids_config_.is_protected_audience_enabled = false;
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal).Times(0);
  EXPECT_CALL(
      protected_app_signals_bidding_client_mock_,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .Times(1);
  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_,
      &protected_app_signals_bidding_client_mock_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
}

TEST_F(GetProtectedAppSignalsTest, GetBidsResponseAggregatedBackToSfe) {
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/true);
  absl::BlockingCounter bids_counter(2);

  SetupBiddingProviderMock(
      bidding_signals_provider_,
      {.bidding_signals_value = kBiddingSignalsToBeReturned});

  EXPECT_CALL(bidding_client_mock_, ExecuteInternal)
      .WillOnce([&bids_counter](
                    std::unique_ptr<GenerateBidsRequest::GenerateBidsRawRequest>
                        get_values_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        auto raw_response =
            std::make_unique<GenerateBidsResponse::GenerateBidsRawResponse>();
        *raw_response->mutable_bids()->Add() = CreateAdWithBid();
        std::move(on_done)(std::move(raw_response),
                           /*response_metadata=*/{});
        bids_counter.DecrementCount();
        return absl::OkStatus();
      });

  EXPECT_CALL(
      protected_app_signals_bidding_client_mock_,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([&bids_counter](
                    std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        auto raw_response =
            std::make_unique<GenerateProtectedAppSignalsBidsRawResponse>();
        *raw_response->mutable_bids()->Add() =
            CreateProtectedAppSignalsAdWithBid();
        std::move(on_done)(std::move(raw_response),
                           /*response_metadata=*/{});
        bids_counter.DecrementCount();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_,
      &protected_app_signals_bidding_client_mock_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  bids_counter.Wait();

  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  ASSERT_EQ(raw_response.bids_size(), 1);
  ASSERT_EQ(raw_response.protected_app_signals_bids_size(), 1);

  // Validate that the protected audience bid contents match.
  const auto& protected_audience_ad_with_bid = raw_response.bids().at(0);
  EXPECT_EQ(protected_audience_ad_with_bid.bid(), kTestBidValue1);
  EXPECT_EQ(protected_audience_ad_with_bid.interest_group_name(),
            kTestInterestGroupName);
  EXPECT_EQ(protected_audience_ad_with_bid.render(), kTestRender1);
  EXPECT_EQ(protected_audience_ad_with_bid.bid(), kTestBidValue1);
  ASSERT_EQ(protected_audience_ad_with_bid.ad_components_size(), 1);
  EXPECT_EQ(protected_audience_ad_with_bid.ad_components().at(0),
            kTestAdComponent);
  EXPECT_EQ(protected_audience_ad_with_bid.bid_currency(), kTestCurrency1);
  EXPECT_EQ(protected_audience_ad_with_bid.ad_cost(), kTestAdCost1);
  EXPECT_EQ(protected_audience_ad_with_bid.modeling_signals(),
            kTestModelingSignals1);
  AdWithBid expected_protected_audience_ad_with_bid;
  expected_protected_audience_ad_with_bid.mutable_ad()
      ->mutable_struct_value()
      ->MergeFrom(
          MakeAnAd(kTestRender1, kTestMetadataKey1, kTestMetadataValue1));
  EXPECT_TRUE(
      MessageDifferencer::Equals(protected_audience_ad_with_bid.ad(),
                                 expected_protected_audience_ad_with_bid.ad()));

  // Validate that the protected app signals bid contents match.
  const auto& protected_app_signals_ad_with_bid =
      raw_response.protected_app_signals_bids().at(0);
  EXPECT_EQ(protected_app_signals_ad_with_bid.render(), kTestRender2);
  EXPECT_EQ(protected_app_signals_ad_with_bid.bid(), kTestBidValue2);
  EXPECT_EQ(protected_app_signals_ad_with_bid.bid_currency(), kTestCurrency2);
  EXPECT_EQ(protected_app_signals_ad_with_bid.ad_cost(), kTestAdCost2);
  ProtectedAppSignalsAdWithBid expected_protected_app_signals_ad_with_bid;
  expected_protected_app_signals_ad_with_bid.mutable_ad()
      ->mutable_struct_value()
      ->MergeFrom(
          MakeAnAd(kTestRender1, kTestMetadataKey1, kTestMetadataValue1));
  EXPECT_TRUE(MessageDifferencer::Equals(
      protected_audience_ad_with_bid.ad(),
      expected_protected_app_signals_ad_with_bid.ad()));
}

TEST_F(GetProtectedAppSignalsTest,
       SkipProtectedAudienceBiddingIfNoInterestGroups) {
  request_ = CreateGetBidsRequest(/*add_protected_signals_input=*/true,
                                  /*add_protected_audience_input=*/false);
  absl::BlockingCounter bids_counter(1);

  EXPECT_CALL(bidding_signals_provider_, Get(_, _, _, _)).Times(0);
  EXPECT_CALL(bidding_client_mock_, ExecuteInternal).Times(0);

  EXPECT_CALL(
      protected_app_signals_bidding_client_mock_,
      ExecuteInternal(
          An<std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>>(),
          An<grpc::ClientContext*>(),
          An<absl::AnyInvocable<
              void(absl::StatusOr<std::unique_ptr<
                       GenerateProtectedAppSignalsBidsRawResponse>>,
                   ResponseMetadata) &&>>(),
          An<absl::Duration>(), An<RequestConfig>()))
      .WillOnce([&bids_counter](
                    std::unique_ptr<GenerateProtectedAppSignalsBidsRawRequest>
                        get_values_raw_request,
                    grpc::ClientContext* context, auto on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        auto raw_response =
            std::make_unique<GenerateProtectedAppSignalsBidsRawResponse>();
        *raw_response->mutable_bids()->Add() =
            CreateProtectedAppSignalsAdWithBid();
        std::move(on_done)(std::move(raw_response),
                           /*response_metadata=*/{});
        bids_counter.DecrementCount();
        return absl::OkStatus();
      });

  GetBidsUnaryReactor class_under_test(
      context_, request_, response_, &bidding_signals_provider_,
      bidding_client_mock_, get_bids_config_,
      &protected_app_signals_bidding_client_mock_, key_fetcher_manager_.get(),
      crypto_client_.get(), kv_async_client_.get(), executor_);
  class_under_test.Execute();
  bids_counter.Wait();

  GetBidsResponse::GetBidsRawResponse raw_response;
  raw_response.ParseFromString(response_.response_ciphertext());
  ASSERT_TRUE(raw_response.bids().empty());
  ASSERT_EQ(raw_response.protected_app_signals_bids_size(), 1);

  // Validate that the protected app signals bid contents match.
  const auto& protected_app_signals_ad_with_bid =
      raw_response.protected_app_signals_bids().at(0);
  EXPECT_EQ(protected_app_signals_ad_with_bid.render(), kTestRender2);
  EXPECT_EQ(protected_app_signals_ad_with_bid.bid(), kTestBidValue2);
  EXPECT_EQ(protected_app_signals_ad_with_bid.bid_currency(), kTestCurrency2);
  EXPECT_EQ(protected_app_signals_ad_with_bid.ad_cost(), kTestAdCost2);
  ProtectedAppSignalsAdWithBid expected_protected_app_signals_ad_with_bid;
  expected_protected_app_signals_ad_with_bid.mutable_ad()
      ->mutable_struct_value()
      ->MergeFrom(
          MakeAnAd(kTestRender1, kTestMetadataKey1, kTestMetadataValue1));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
