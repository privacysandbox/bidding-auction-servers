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

#include <math.h>

#include <memory>
#include <set>
#include <utility>

#include <gmock/gmock-matchers.h>
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
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/cpp/communication/encoding_utils.h"
#include "src/cpp/communication/ohttp_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::util::MessageDifferencer;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;
using EncodedBueryInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBueryInputs = ::google::protobuf::Map<std::string, BuyerInput>;
using GetBidDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>) &&>;
using ScoreAdsDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                ScoreAdsResponse::ScoreAdsRawResponse>>) &&>;

inline constexpr int kTestBidValue = 10.0;
inline constexpr int kTestAdCost = 2.0;
inline constexpr int kTestEncodingVersion = 1;
inline constexpr int kTestModelingSignals = 3;
inline constexpr char kTestEgressFeature[] = "TestEgressFeatures";
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
        server_common::telemetry::BuildDependentConfig(config_proto));
    config_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_.SetFlagForTest("", CONSENTED_DEBUG_TOKEN);
    config_.SetFlagForTest(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
  }

  TrustedServersConfigClient config_ = CreateConfig();
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

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(),
      request_with_context.context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->GetPlaintextData().size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
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

  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(),
      request_with_context.context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->GetPlaintextData().size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
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
  auto ohttp_request = CreateValidEncryptedRequest(std::move(*framed_request));
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
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(), context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status().message();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->GetPlaintextData().size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
  ASSERT_TRUE(unframed_response.ok()) << unframed_response.status().message();

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      GzipDecompress(unframed_response->compressed_data);
  EXPECT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();

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
        server_common::telemetry::BuildDependentConfig(config_proto));
    config_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_.SetFlagForTest("", CONSENTED_DEBUG_TOKEN);
    config_.SetFlagForTest(kTrue, ENABLE_PROTECTED_APP_SIGNALS);

    EXPECT_CALL(*key_fetcher_manager_, GetPrivateKey)
        .WillRepeatedly(Return(GetPrivateKey()));
    log::PS_VLOG_IS_ON(0, 10);
  }

  // This could return any valid byte string.
  std::string GetTestAppSignals() {
    ProtectedAppSignals protected_app_signals;
    protected_app_signals.set_encoding_version(kTestEncodingVersion);
    return protected_app_signals.SerializeAsString();
  }

  ProtectedAppSignalsAdWithBid GetTestPASAdWithBid() {
    ProtectedAppSignalsAdWithBid result;
    result.mutable_ad()->mutable_struct_value()->MergeFrom(
        MakeAnAd(kTestRender, kTestMetadataKey, kTestMetadataValue));
    result.set_bid(kTestBidValue);
    result.set_render(kTestRender);
    result.set_modeling_signals(kTestModelingSignals);
    result.set_ad_cost(kTestAdCost);
    result.set_egress_features(kTestEgressFeature);
    return result;
  }

  // Creates a SelectAdRequest with PA + PAS Buyer Input.
  EncryptedSelectAdRequestWithContext<ProtectedAuctionInput>
  CreateSelectAdRequest(
      absl::string_view seller_origin_domain, bool add_interest_group = true,
      bool add_protected_app_signals = true,
      std::optional<std::string> app_install_signals = std::nullopt) {
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
    protected_auction_input.set_generation_id(kSampleGenerationId);
    *protected_auction_input.mutable_buyer_input() =
        std::move(encoded_buyer_inputs);
    protected_auction_input.set_publisher_name(MakeARandomString());

    SelectAdRequest request;
    request.mutable_auction_config()->set_seller_signals(
        absl::StrCat("{\"seller_signal\": \"", MakeARandomString(), "\"}"));
    request.mutable_auction_config()->set_auction_signals(
        absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));
    request.mutable_auction_config()->set_seller(seller_origin_domain);
    request.set_client_type(CLIENT_TYPE_ANDROID);
    for (const auto& [local_buyer, unused] :
         protected_auction_input.buyer_input()) {
      *request.mutable_auction_config()->mutable_buyer_list()->Add() =
          local_buyer;
    }

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
  ClientRegistry clients_{scoring_signals_provider_, scoring_client_,
                          buyer_front_end_async_client_factory_mock_,
                          *key_fetcher_manager_,
                          std::make_unique<MockAsyncReporter>(
                              std::make_unique<MockHttpFetcherAsync>())};
};

TEST_F(SelectAdReactorPASTest, PASBuyerInputIsPopulatedForGetBids) {
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
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
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
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
  config_.SetFlagForTest(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);
  auto mock_get_bids = [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                              get_bids_raw_request,
                          const RequestMetadata& metadata,
                          GetBidDoneCallback on_done, absl::Duration timeout) {
    // Expect PAS buyer inputs to be not present in GetBids.
    EXPECT_FALSE(get_bids_raw_request->has_protected_app_signals_buyer_input());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
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
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);
  const auto& select_ad_req = request_with_context.select_ad_request;
  const auto& protected_auction_input =
      request_with_context.protected_auction_input;

  // Setup BFE to return a PAS bid.
  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
    auto response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    response->mutable_protected_app_signals_bids()->Add(GetTestPASAdWithBid());
    std::move(on_done)(std::move(response));
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  EXPECT_CALL(scoring_client_, ExecuteInternal)
      .WillOnce(
          [this, &select_ad_req, &protected_auction_input](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              const RequestMetadata& metadata, ScoreAdsDoneCallback on_done,
              absl::Duration timeout) {
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
            EXPECT_EQ(observed_bid_with_metadata.egress_features(),
                      expected_bid.egress_features());
            EXPECT_EQ(observed_bid_with_metadata.owner(), kSampleBuyer);
            EXPECT_TRUE(MessageDifferencer::Equals(
                observed_bid_with_metadata.ad(), expected_bid.ad()));
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
  config_.SetFlagForTest(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
  auto request_with_context = CreateSelectAdRequest(kSellerOriginDomain);
  const auto& select_ad_req = request_with_context.select_ad_request;
  const auto& protected_auction_input =
      request_with_context.protected_auction_input;

  // Setup BFE to return a PAS bid -- though this will be an error in itself
  // since the feature is disabled but we still cover this possible error case.
  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
    auto response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    response->mutable_protected_app_signals_bids()->Add(GetTestPASAdWithBid());
    std::move(on_done)(std::move(response));
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock_, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  // Verify call to scoring client is not made since there are not PA bids
  // and though BFE returned PAS bids (possibly erroneously), we don't send
  // them for scoring.
  EXPECT_CALL(scoring_client_, ExecuteInternal).Times(0);
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForApp>(
          config_, clients_, request_with_context.select_ad_request);
}

TEST_F(SelectAdReactorPASTest, PASOnlyBuyerInputIsAllowed) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain, /*add_interest_group=*/false);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
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
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
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
  AuctionResult auction_result;
  auction_result.ParseFromString(
      encrypted_response.auction_result_ciphertext());
  EXPECT_FALSE(auction_result.has_error());
}

TEST_F(SelectAdReactorPASTest, BothPASAndPAInputsMissingIsAnError) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain, /*add_interest_group=*/false,
                            /*add_protected_app_signals=*/false);

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
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
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
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
  auto decrypted_response = DecryptEncapsulatedResponse(
      encrypted_response.auction_result_ciphertext(),
      request_with_context.context);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Unframe the framed response.
  absl::StatusOr<server_common::DecodedRequest> unframed_response =
      server_common::DecodeRequestPayload(
          decrypted_response->GetPlaintextData());
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

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
    // Expect PAS buyer inputs to not be be populated in GetBids.
    EXPECT_FALSE(get_bids_raw_request->has_protected_app_signals_buyer_input());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
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
  AuctionResult auction_result;
  auction_result.ParseFromString(
      encrypted_response.auction_result_ciphertext());
  EXPECT_FALSE(auction_result.has_error());
}

TEST_F(SelectAdReactorPASTest,
       EmptyPASBuyerInputMeansMissingPASSignalsInBuyerRequest) {
  auto request_with_context =
      CreateSelectAdRequest(kSellerOriginDomain,
                            /*add_interest_group=*/true,
                            /*add_protected_app_signals=*/true,
                            /*app_install_signals=*/"");

  auto mock_get_bids = [this](std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                                  get_bids_raw_request,
                              const RequestMetadata& metadata,
                              GetBidDoneCallback on_done,
                              absl::Duration timeout) {
    // Expect PAS buyer inputs to not be be populated in GetBids.
    EXPECT_FALSE(get_bids_raw_request->has_protected_app_signals_buyer_input());

    // Ensure PA buyer inputs doesn't have the PAS data.
    EXPECT_FALSE(
        get_bids_raw_request->buyer_input().has_protected_app_signals());
    return absl::OkStatus();
  };
  auto setup_mock_buyer =
      [&mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal(_, _, _, _))
            .WillRepeatedly(mock_get_bids);
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
  AuctionResult auction_result;
  auction_result.ParseFromString(
      encrypted_response.auction_result_ciphertext());
  EXPECT_FALSE(auction_result.has_error());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
