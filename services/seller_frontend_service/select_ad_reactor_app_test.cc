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
#include "services/seller_frontend_service/select_ad_reactor_app.h"

#include <gmock/gmock-matchers.h>

#include <math.h>

#include <memory>
#include <set>
#include <utility>

#include <google/protobuf/util/json_util.h>
#include <include/gmock/gmock-actions.h>
#include <include/gmock/gmock-nice-strict.h>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/compression/gzip.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/util/oblivious_http_utils.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/communication/ohttp_utils.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::util::MessageDifferencer;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;
using EncodedBueryInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBueryInputs = ::google::protobuf::Map<std::string, BuyerInput>;
using GetBidDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>,
         ResponseMetadata response_metadata) &&>;
using ScoreAdsDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
         ResponseMetadata response_metadata) &&>;

inline constexpr int kTestBidValue = 10.0;
inline constexpr int kTestAdCost = 2.0;
inline constexpr int kTestEncodingVersion = 1;
inline constexpr int kTestModelingSignals = 3;
inline constexpr char kTestEgressPayload[] = "TestegressPayload";
inline constexpr char kTestTemporaryEgressPayload[] =
    "TestTemporaryEgressPayload";
inline constexpr char kTestRender[] = "https://test-render.com";
inline constexpr char kAdRenderUrls[] = "AdRenderUrls";
inline constexpr char kTestMetadataKey[] = "TestMetadataKey";
inline constexpr int kTestMetadataValue = 53;

template <typename T>
class SelectAdReactorForAppTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
    config_.SetOverride("", CONSENTED_DEBUG_TOKEN);
    config_.SetOverride(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
    config_.SetOverride(kFalse, ENABLE_CHAFFING);
  }

  TrustedServersConfigClient config_ = CreateConfig();
  const HpkeKeyset default_keyset_ = HpkeKeyset{};
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(SelectAdReactorForAppTest, ProtectedAuctionInputTypes);

TYPED_TEST(SelectAdReactorForAppTest, VerifyEncoding) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          CLIENT_TYPE_ANDROID, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  ASSERT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
  EXPECT_EQ(deserialized_auction_result.ad_type(),
            AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);

  // Validate that the bidding groups data is not present.
  EXPECT_EQ(deserialized_auction_result.bidding_groups().size(), 0);
  EXPECT_TRUE(deserialized_auction_result.win_reporting_urls()
                  .top_level_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(deserialized_auction_result.win_reporting_urls()
                  .top_level_seller_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
  EXPECT_TRUE(deserialized_auction_result.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(deserialized_auction_result.win_reporting_urls()
                  .buyer_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
  EXPECT_TRUE(deserialized_auction_result.win_reporting_urls()
                  .component_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(deserialized_auction_result.win_reporting_urls()
                  .component_seller_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
}

TYPED_TEST(SelectAdReactorForAppTest, VerifyEncodingWithReportingUrls) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  bool expect_all_buyers_solicited = true;
  absl::string_view top_level_seller = "";
  bool enable_reporting = true;
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          CLIENT_TYPE_ANDROID, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          expect_all_buyers_solicited, top_level_seller, enable_reporting);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(*decrypted_response);
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
  EXPECT_EQ(deserialized_auction_result.ad_type(),
            AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);

  // Validate that the bidding groups data is not present.
  EXPECT_EQ(deserialized_auction_result.bidding_groups().size(), 0);
  EXPECT_EQ(deserialized_auction_result.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestTopLevelSellerReportingUrl);
  EXPECT_EQ(deserialized_auction_result.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent),
            kTestInteractionUrl);
  EXPECT_EQ(deserialized_auction_result.win_reporting_urls()
                .buyer_reporting_urls()
                .reporting_url(),
            kTestBuyerReportingUrl);
  EXPECT_EQ(deserialized_auction_result.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent),
            kTestInteractionUrl);
  EXPECT_EQ(deserialized_auction_result.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentSellerReportingUrl);
  EXPECT_EQ(deserialized_auction_result.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent),
            kTestInteractionUrl);
}

TYPED_TEST(SelectAdReactorForAppTest, VerifyEncodingForServerComponentAuction) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  EXPECT_CALL(*key_fetcher_manager, GetPublicKey)
      .WillOnce(Return(google::cmrt::sdk::public_key_service::v1::PublicKey()));
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClient(crypto_client);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          CLIENT_TYPE_ANDROID, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, kTestTopLevelSellerOriginDomain,
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          {&crypto_client,
           EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_GCP});

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients, request_with_context.select_ad_request);
  ASSERT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  absl::string_view decrypted_response =
      encrypted_response.auction_result_ciphertext();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response.size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(decrypted_response);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_EQ(deserialized_auction_result.ad_type(),
            AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);

  // Validate chaff bit is not set and error is not populated.
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
  EXPECT_FALSE(deserialized_auction_result.has_error());

  // Validate server component auction fields.
  EXPECT_EQ(deserialized_auction_result.auction_params().component_seller(),
            kSellerOriginDomain);
  EXPECT_EQ(
      deserialized_auction_result.auction_params().ciphertext_generation_id(),
      kSampleGenerationId);
}

TYPED_TEST(SelectAdReactorForAppTest, VerifyChaffedResponse) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          CLIENT_TYPE_ANDROID, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe and decompress the framed response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  ASSERT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));

  // Validate chaff bit is set in response.
  EXPECT_TRUE(deserialized_auction_result.is_chaff());
}

TYPED_TEST(SelectAdReactorForAppTest, VerifyErrorForProtoDecodingFailure) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          CLIENT_TYPE_ANDROID, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/false);
  auto& protected_auction_input = request_with_context.protected_auction_input;
  auto& request = request_with_context.select_ad_request;
  // Set up the encoded cipher text in the request.
  std::string encoded_request = protected_auction_input.SerializeAsString();
  // Corrupt the binary proto so that we can verify a proper error is set in
  // the response.
  encoded_request.data()[0] = 'a';
  absl::StatusOr<std::string> framed_request =
      server_common::EncodeResponsePayload(
          server_common::CompressionType::kGzip, encoded_request,
          GetEncodedDataSize(encoded_request.size()));
  EXPECT_TRUE(framed_request.ok()) << framed_request.status().message();
  auto ohttp_request =
      CreateValidEncryptedRequest(*framed_request, this->default_keyset_);
  EXPECT_TRUE(ohttp_request.ok()) << ohttp_request.status().message();
  std::string encrypted_request =
      '\0' + ohttp_request->EncapsulateAndSerialize();
  auto context = std::move(*ohttp_request).ReleaseContext();
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_request);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(this->config_, clients, request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(), context,
      kBiddingAuctionOhttpResponseLabel);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  ASSERT_TRUE(decompressed_response.ok());

  // Validate the error message returned in the response.
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_EQ(deserialized_auction_result.error().message(),
            kBadProtectedAudienceBinaryProto);
  EXPECT_EQ(deserialized_auction_result.error().code(), 400);

  // Validate chaff bit is not set if there was an input validation error.
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
}

class SelectAdReactorPASTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
    config_.SetOverride("", CONSENTED_DEBUG_TOKEN);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
    config_.SetOverride(kFalse, ENABLE_CHAFFING);

    EXPECT_CALL(*key_fetcher_manager_, GetPrivateKey)
        .WillRepeatedly(Return(GetPrivateKey()));
    server_common::log::SetGlobalPSVLogLevel(10);
  }

  // This could return any valid byte string.
  std::string GetTestAppSignals() {
    ProtectedAppSignals protected_app_signals;
    protected_app_signals.set_encoding_version(kTestEncodingVersion);
    return protected_app_signals.SerializeAsString();
  }

  AdWithBid GetTestPAAdWithBid() {
    AdWithBid result;
    result.mutable_ad()->mutable_struct_value()->MergeFrom(
        MakeAnAd(kTestRender, kTestMetadataKey, kTestMetadataValue));
    result.set_bid(kTestBidValue);
    result.set_render(kTestRender);
    result.set_modeling_signals(kTestModelingSignals);
    result.set_ad_cost(kTestAdCost);
    return result;
  }

  ProtectedAppSignalsAdWithBid GetTestPASAdWithBid() {
    ProtectedAppSignalsAdWithBid result;
    result.mutable_ad()->mutable_struct_value()->MergeFrom(
        MakeAnAd(kTestRender, kTestMetadataKey, kTestMetadataValue));
    result.set_bid(kTestBidValue);
    result.set_render(kTestRender);
    result.set_modeling_signals(kTestModelingSignals);
    result.set_ad_cost(kTestAdCost);
    result.set_egress_payload(kTestEgressPayload);
    result.set_temporary_unlimited_egress_payload(kTestTemporaryEgressPayload);
    return result;
  }

  std::pair<SelectAdRequest, ProtectedAuctionInput> CreateRawSelectAdRequest(
      absl::string_view seller_origin_domain, bool add_interest_group = true,
      bool add_protected_app_signals = true,
      std::optional<absl::string_view> app_install_signals = std::nullopt,
      bool add_contextual_pas_ad_render_ids = false,
      bool enforce_kanon = false) {
    BuyerInput buyer_input;

    if (add_interest_group) {
      // PA Buyer Inputs.
      auto* interest_group = buyer_input.mutable_interest_groups()->Add();
      interest_group->set_name(kSampleInterestGroupName);
      *interest_group->mutable_bidding_signals_keys()->Add() = "[]";
    }

    if (add_protected_app_signals) {
      // PAS Buyer Inputs.
      auto* protected_app_signals = buyer_input.mutable_protected_app_signals();
      protected_app_signals->set_encoding_version(kTestEncodingVersion);
      protected_app_signals->set_app_install_signals(
          app_install_signals.has_value() ? *app_install_signals
                                          : GetTestAppSignals());
    }

    DecodedBueryInputs decoded_buyer_inputs;
    decoded_buyer_inputs.emplace(kSampleBuyer, buyer_input);
    EncodedBueryInputs encoded_buyer_inputs =
        GetProtoEncodedBuyerInputs(decoded_buyer_inputs);

    ProtectedAuctionInput protected_auction_input;
    protected_auction_input.set_enforce_kanon(enforce_kanon);
    protected_auction_input.set_generation_id(kSampleGenerationId);
    *protected_auction_input.mutable_buyer_input() =
        std::move(encoded_buyer_inputs);
    protected_auction_input.set_publisher_name(MakeARandomString());

    SelectAdRequest request;
    auto* auction_config = request.mutable_auction_config();
    auction_config->set_seller_signals(
        absl::StrCat("{\"seller_signal\": \"", MakeARandomString(), "\"}"));
    auction_config->set_auction_signals(
        absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));
    auction_config->set_seller(seller_origin_domain);
    request.set_client_type(CLIENT_TYPE_ANDROID);
    for (const auto& [local_buyer, unused] :
         protected_auction_input.buyer_input()) {
      *auction_config->mutable_buyer_list()->Add() = local_buyer;
      if (add_contextual_pas_ad_render_ids) {
        auto& buyer_config =
            (*auction_config->mutable_per_buyer_config())[local_buyer];
        auto* contextual_protected_app_signals_data =
            buyer_config.mutable_contextual_protected_app_signals_data();
        *contextual_protected_app_signals_data->mutable_ad_render_ids()->Add() =
            kSampleContextualPasAdId;
      }
    }
    return {std::move(request), std::move(protected_auction_input)};
  }

  // Creates a SelectAdRequest with PA + PAS Buyer Input.
  EncryptedSelectAdRequestWithContext<ProtectedAuctionInput>
  CreateSelectAdRequest(
      absl::string_view seller_origin_domain, bool add_interest_group = true,
      bool add_protected_app_signals = true,
      std::optional<absl::string_view> app_install_signals = std::nullopt,
      bool enforce_kanon = false) {
    auto [request, protected_auction_input] = CreateRawSelectAdRequest(
        seller_origin_domain, add_interest_group, add_protected_app_signals,
        app_install_signals, /*add_contextual_pas_ad_render_ids=*/false,
        enforce_kanon);
    auto [encrypted_request, context] =
        GetProtoEncodedEncryptedInputAndOhttpContext(protected_auction_input);
    *request.mutable_protected_auction_ciphertext() =
        std::move(encrypted_request);
    return {std::move(protected_auction_input), std::move(request),
            std::move(context)};
  }

  // Creates a SelectAdRequest with PA + PAS Buyer Input.
  EncryptedSelectAdRequestWithContext<ProtectedAuctionInput>
  CreateSelectAdRequestWithContextualPasAds(
      absl::string_view seller_origin_domain, bool add_interest_group = true) {
    auto [request, protected_auction_input] = CreateRawSelectAdRequest(
        seller_origin_domain, add_interest_group,
        /*add_protected_app_signals=*/true, GetTestAppSignals(),
        /*add_contextual_pas_ad_render_ids=*/true);
    auto [encrypted_request, context] =
        GetProtoEncodedEncryptedInputAndOhttpContext(protected_auction_input);
    *request.mutable_protected_auction_ciphertext() =
        std::move(encrypted_request);
    return {std::move(protected_auction_input), std::move(request),
            std::move(context)};
  }

  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider_;
  TrustedServersConfigClient config_ = CreateConfig();
  BuyerFrontEndAsyncClientFactoryMock
      buyer_front_end_async_client_factory_mock_;
  ScoringAsyncClientMock scoring_client_;
  BuyerBidsResponseMap expected_buyer_bids_;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager_ =
      std::make_unique<server_common::MockKeyFetcherManager>();
  ClientRegistry clients_{scoring_signals_provider_,
                          scoring_client_,
                          buyer_front_end_async_client_factory_mock_,
                          *key_fetcher_manager_,
                          /* *crypto_client = */ nullptr,
                          std::make_unique<MockAsyncReporter>(
                              std::make_unique<MockHttpFetcherAsync>())};

  const HpkeKeyset default_keyset_ = HpkeKeyset{};
};

TEST_F(SelectAdReactorPASTest, PASBuyerInputIsPopulatedForGetBids) {
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    // Expect PAS buyer inputs to be populated correctly in GetBids.
    EXPECT_TRUE(get_bids_raw_request->has_protected_app_signals_buyer_input());
    EXPECT_TRUE(get_bids_raw_request->protected_app_signals_buyer_input()
                    .has_protected_app_signals());
    auto protected_app_signals =
        get_bids_raw_request->protected_app_signals_buyer_input()
            .protected_app_signals();
    EXPECT_EQ(protected_app_signals.encoding_version(), kTestEncodingVersion);
    EXPECT_EQ(protected_app_signals.app_install_signals(), GetTestAppSignals());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

TEST_F(SelectAdReactorPASTest, PASBuyerInputIsClearedIfFeatureNotAvailable) {
  config_.SetOverride(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);
  auto mock_get_bids = [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                              get_bids_raw_request,
                          grpc::ClientContext* context,
                          GetBidDoneCallback on_done, absl::Duration timeout,
                          RequestConfig request_config) {
    // Expect PAS buyer inputs to be populated correctly in GetBids.
    EXPECT_FALSE(get_bids_raw_request->has_protected_app_signals_buyer_input());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

TEST_F(SelectAdReactorPASTest, PASAdWithBidIsSentForScoring) {
  absl::SetFlag(&FLAGS_enable_kanon, true);
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain,
                            /*add_interest_group=*/true,
                            /*add_protected_app_signals=*/true,
                            /*app_install_signals=*/std::nullopt,
                            /*enforce_kanon=*/true);
  const auto& select_ad_req = request_with_context.select_ad_request;
  const auto& protected_auction_input =
      request_with_context.protected_auction_input;

  // Setup BFE to return a PAS bid.
  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    auto response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    response->mutable_protected_app_signals_bids()->Add(GetTestPASAdWithBid());
    std::move(on_done)(std::move(response), /* response_metadata= */ {});
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock_);

  EXPECT_CALL(scoring_client_, ExecuteInternal)
      .WillOnce(
          [this, &select_ad_req, &protected_auction_input](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            EXPECT_EQ(request->publisher_hostname(),
                      protected_auction_input.publisher_name());
            EXPECT_EQ(request->seller_signals(),
                      select_ad_req.auction_config().seller_signals());
            EXPECT_EQ(request->auction_signals(),
                      select_ad_req.auction_config().auction_signals());
            EXPECT_EQ(request->scoring_signals(), kAdRenderUrls);
            EXPECT_EQ(request->protected_app_signals_ad_bids().size(), 1);

            const auto& observed_bid_with_metadata =
                request->protected_app_signals_ad_bids().at(0);
            const auto expected_bid = GetTestPASAdWithBid();
            EXPECT_EQ(observed_bid_with_metadata.bid(), expected_bid.bid());
            EXPECT_EQ(observed_bid_with_metadata.render(),
                      expected_bid.render());
            EXPECT_EQ(observed_bid_with_metadata.modeling_signals(),
                      expected_bid.modeling_signals());
            EXPECT_EQ(observed_bid_with_metadata.ad_cost(),
                      expected_bid.ad_cost());
            EXPECT_EQ(observed_bid_with_metadata.egress_payload(),
                      expected_bid.egress_payload());
            EXPECT_EQ(observed_bid_with_metadata.owner(), kSampleBuyer);
            EXPECT_TRUE(MessageDifferencer::Equals(
                observed_bid_with_metadata.ad(), expected_bid.ad()));
            // k-anon status is not implemented yet and if k-anon is enabled and
            // enforced, we default the k-anon status to false for the bid.
            EXPECT_FALSE(observed_bid_with_metadata.k_anon_status());
            std::move(on_done)(
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
            return absl::OkStatus();
          });

  auto expected_get_bids_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  expected_get_bids_response->mutable_protected_app_signals_bids()->Add(
      GetTestPASAdWithBid());
  expected_buyer_bids_.emplace(kSampleBuyer,
                               std::move(expected_get_bids_response));
  SetupScoringProviderMock(scoring_signals_provider_, expected_buyer_bids_,
                           kAdRenderUrls);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

TEST_F(SelectAdReactorPASTest,
       PASAdWithBidIsNotSentForScoringWhenFeatureDisabled) {
  config_.SetOverride(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);

  // Setup BFE to return a PAS bid -- though this will be an error in itself
  // since the feature is disabled but we still cover this possible error case.
  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    auto response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    response->mutable_protected_app_signals_bids()->Add(GetTestPASAdWithBid());
    std::move(on_done)(std::move(response), /* response_metadata= */ {});
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  // Verify call to scoring client is not made since there are no PA bids
  // and though BFE returned PAS bids (possibly erroneously), we don't send
  // them for scoring.
  EXPECT_CALL(scoring_client_, ExecuteInternal).Times(0);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

void VerifySelectedAdResponseSuccess(
    quiche::ObliviousHttpRequest::Context& context,
    SelectAdResponse& encrypted_response) {
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(), context,
      kBiddingAuctionOhttpResponseLabel);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();

  AuctionResult auction_result;
  EXPECT_TRUE(auction_result.ParseFromArray(decompressed_response->data(),
                                            decompressed_response->size()));
  EXPECT_FALSE(auction_result.has_error());
}

TEST_F(SelectAdReactorPASTest, PASOnlyBuyerInputIsAllowed) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain, /*add_interest_group=*/false);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    // Expect PAS buyer inputs to be populated correctly in GetBids.
    EXPECT_TRUE(get_bids_raw_request->has_protected_app_signals_buyer_input());
    EXPECT_TRUE(get_bids_raw_request->protected_app_signals_buyer_input()
                    .has_protected_app_signals());
    auto protected_app_signals =
        get_bids_raw_request->protected_app_signals_buyer_input()
            .protected_app_signals();
    EXPECT_EQ(protected_app_signals.encoding_version(), kTestEncodingVersion);
    EXPECT_EQ(protected_app_signals.app_install_signals(), GetTestAppSignals());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
  VerifySelectedAdResponseSuccess(request_with_context.context,
                                  encrypted_response);
}

TEST_F(SelectAdReactorPASTest, BothPASAndPAInputsMissingIsAnError) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain, /*add_interest_group=*/false,
                            /*add_protected_app_signals=*/false);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    // Expect PAS buyer inputs to be populated correctly in GetBids.
    EXPECT_TRUE(get_bids_raw_request->has_protected_app_signals_buyer_input());
    EXPECT_TRUE(get_bids_raw_request->protected_app_signals_buyer_input()
                    .has_protected_app_signals());
    auto protected_app_signals =
        get_bids_raw_request->protected_app_signals_buyer_input()
            .protected_app_signals();
    EXPECT_EQ(protected_app_signals.encoding_version(), kTestEncodingVersion);
    EXPECT_EQ(protected_app_signals.app_install_signals(), GetTestAppSignals());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients_, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(*decrypted_response);
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
  EXPECT_TRUE(deserialized_auction_result.has_error());
  EXPECT_THAT(deserialized_auction_result.error().message(),
              HasSubstr("Request is missing interest groups and protected "
                        "signals for buyer"));
}

TEST_F(SelectAdReactorPASTest,
       MissingPASBuyerInputMeansMissingPASSignalsInBuyerRequest) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain, /*add_interest_group=*/true,
                            /*add_protected_app_signals=*/false);

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
             get_bids_raw_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout,
         RequestConfig request_config) {  // Expect PAS buyer inputs to not be
                                          // be populated in GetBids.
        EXPECT_FALSE(
            get_bids_raw_request->has_protected_app_signals_buyer_input());

        // Ensure PA buyer inputs doesn't have the PAS data.
        EXPECT_FALSE(
            get_bids_raw_request->buyer_input().has_protected_app_signals());
        return absl::OkStatus();
      };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
  VerifySelectedAdResponseSuccess(request_with_context.context,
                                  encrypted_response);
}

TEST_F(SelectAdReactorPASTest,
       EmptyPASBuyerInputMeansMissingPASSignalsInBuyerRequest) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain,
                            /*add_interest_group=*/true,
                            /*add_protected_app_signals=*/true,
                            /*app_install_signals=*/"");

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
             get_bids_raw_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout,
         RequestConfig request_config) {  // Expect PAS buyer inputs to not be
                                          // be populated in GetBids.
        EXPECT_FALSE(
            get_bids_raw_request->has_protected_app_signals_buyer_input());

        // Ensure PA buyer inputs doesn't have the PAS data.
        EXPECT_FALSE(
            get_bids_raw_request->buyer_input().has_protected_app_signals());
        return absl::OkStatus();
      };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);

  VerifySelectedAdResponseSuccess(request_with_context.context,
                                  encrypted_response);
}

TEST_F(SelectAdReactorPASTest,
       PASBuyerInputWithContextualAdIsPopulatedForGetBids) {
  auto request_with_context =
      CreateSelectAdRequestWithContextualPasAds(kSellerOriginDomain);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    // Expect PAS buyer inputs to be populated correctly in GetBids.
    EXPECT_TRUE(get_bids_raw_request->has_protected_app_signals_buyer_input());
    EXPECT_TRUE(get_bids_raw_request->protected_app_signals_buyer_input()
                    .has_protected_app_signals());
    auto protected_app_signals =
        get_bids_raw_request->protected_app_signals_buyer_input()
            .protected_app_signals();
    EXPECT_EQ(protected_app_signals.encoding_version(), kTestEncodingVersion);
    EXPECT_EQ(protected_app_signals.app_install_signals(), GetTestAppSignals());

    EXPECT_TRUE(get_bids_raw_request->protected_app_signals_buyer_input()
                    .has_contextual_protected_app_signals_data());
    auto contextual_protected_app_signals_data =
        get_bids_raw_request->protected_app_signals_buyer_input()
            .contextual_protected_app_signals_data();
    EXPECT_EQ(contextual_protected_app_signals_data.ad_render_ids_size(), 1);
    EXPECT_EQ(contextual_protected_app_signals_data.ad_render_ids().at(0),
              kSampleContextualPasAdId);

    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

TEST_F(SelectAdReactorPASTest,
       ProtectedAudienceAdWithBidIsNotSentForScoringWhenFeatureDisabled) {
  config_.SetOverride(kFalse, ENABLE_PROTECTED_AUDIENCE);
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);

  // Setup BFE to return a PA bid and no PAS bid.
  auto mock_get_bids =
      [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                 get_bids_raw_request,
             grpc::ClientContext* context, GetBidDoneCallback on_done,
             absl::Duration timeout, RequestConfig request_config) {
        auto response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        response->mutable_bids()->Add(GetTestPAAdWithBid());
        std::move(on_done)(std::move(response), /* response_metadata= */ {});
        return absl::OkStatus();
      };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  // Verify call to scoring client is not made since there are no PAS bids
  // and though BFE returned PA bids (possibly erroneously), we don't send
  // them for scoring.
  EXPECT_CALL(scoring_client_, ExecuteInternal).Times(0);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

TEST_F(SelectAdReactorPASTest,
       InterestGroupsRemovedIfProtectedAudienceDisabled) {
  config_.SetOverride(kFalse, ENABLE_PROTECTED_AUDIENCE);
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain, /*add_interest_group=*/true);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              grpc::ClientContext* context,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout,
                              RequestConfig request_config) {
    // Expect no interest groups are present.
    EXPECT_EQ(get_bids_raw_request->buyer_input().interest_groups_size(), 0);

    // Expect PAS buyer inputs to be populated correctly in GetBids.
    EXPECT_TRUE(get_bids_raw_request->has_protected_app_signals_buyer_input());
    EXPECT_TRUE(get_bids_raw_request->protected_app_signals_buyer_input()
                    .has_protected_app_signals());
    auto protected_app_signals =
        get_bids_raw_request->protected_app_signals_buyer_input()
            .protected_app_signals();
    EXPECT_EQ(protected_app_signals.encoding_version(), kTestEncodingVersion);
    EXPECT_EQ(protected_app_signals.app_install_signals(), GetTestAppSignals());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal).WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
  VerifySelectedAdResponseSuccess(request_with_context.context,
                                  encrypted_response);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
