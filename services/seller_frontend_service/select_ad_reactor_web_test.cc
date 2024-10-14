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

#include "services/seller_frontend_service/select_ad_reactor_web.h"

#include <gmock/gmock-matchers.h>

#include <math.h>

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <google/protobuf/util/json_util.h>
#include <include/gmock/gmock-actions.h>
#include <include/gmock/gmock-nice-strict.h>

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/oblivious_http_utils.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kSampleBidValue = 10.0;
constexpr char kSampleScoringSignals[] = "sampleScoringSignals";

using ::testing::_;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using Context = ::quiche::ObliviousHttpRequest::Context;
using GetBidDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>,
         ResponseMetadata) &&>;
using ScoringSignalsDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<ScoringSignals>>,
                            GetByteSize) &&>;

template <typename T>
class SelectAdReactorForWebTest : public ::testing::Test {
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

  void SetProtectedAuctionCipherText(const T& protected_auction_input,
                                     const std::string& ciphertext,
                                     SelectAdRequest& select_ad_request) {
    const auto* descriptor = protected_auction_input.GetDescriptor();
    if (descriptor->name() == kProtectedAuctionInput) {
      *select_ad_request.mutable_protected_auction_ciphertext() = ciphertext;
    } else {
      *select_ad_request.mutable_protected_audience_ciphertext() = ciphertext;
    }
  }

  TrustedServersConfigClient config_ = CreateConfig();
  const HpkeKeyset default_keyset_ = HpkeKeyset{};
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(SelectAdReactorForWebTest, ProtectedAuctionInputTypes);

template <typename T>
std::string MessageToJson(const T& message) {
  std::string response_string;
  auto response_status =
      google::protobuf::util::MessageToJsonString(message, &response_string);
  EXPECT_TRUE(response_status.ok()) << "Failed to convert the message to JSON";
  return response_string;
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyCborEncoding) {
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
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  EXPECT_TRUE(decompressed_response.ok());

  std::string base64_response;
  absl::Base64Escape(*decompressed_response, &base64_response);
  ABSL_LOG(INFO) << "Decrypted, decompressed but CBOR encoded auction result:\n"
                 << base64_response;

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok());
  EXPECT_FALSE(deserialized_auction_result->is_chaff());

  ABSL_LOG(INFO) << "Decrypted, decompressed and CBOR decoded auction result:\n"
                 << MessageToJson(*deserialized_auction_result);

  // Validate that the bidding groups data is present.
  EXPECT_EQ(deserialized_auction_result->bidding_groups().size(), 1);
  const auto& [observed_buyer, interest_groups] =
      *deserialized_auction_result->bidding_groups().begin();
  EXPECT_EQ(observed_buyer, kSampleBuyer);
  std::set<int> observed_interest_group_indices(interest_groups.index().begin(),
                                                interest_groups.index().end());
  std::set<int> expected_interest_group_indices = {0};
  std::set<int> unexpected_interest_group_indices;
  absl::c_set_difference(
      observed_interest_group_indices, expected_interest_group_indices,
      std::inserter(unexpected_interest_group_indices,
                    unexpected_interest_group_indices.begin()));
  EXPECT_TRUE(unexpected_interest_group_indices.empty());
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyChaffedResponse) {
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
          CLIENT_TYPE_BROWSER, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);

  EXPECT_FALSE(response_with_cbor.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  EXPECT_TRUE(decompressed_response.ok());
  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok())
      << deserialized_auction_result.status();

  // Validate that the bidding groups data is present.
  EXPECT_TRUE(deserialized_auction_result->is_chaff());
}

auto EqLogContext(const server_common::LogContext& log_context) {
  return AllOf(Property(&server_common::LogContext::generation_id,
                        Eq(log_context.generation_id())),
               Property(&server_common::LogContext::adtech_debug_id,
                        Eq(log_context.adtech_debug_id())));
}

auto EqGetBidsRawRequestWithLogContext(
    const GetBidsRequest::GetBidsRawRequest& raw_request) {
  return AllOf(Property(&GetBidsRequest::GetBidsRawRequest::log_context,
                        EqLogContext(raw_request.log_context())));
}

auto EqScoreAdsRawRequestWithLogContext(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request) {
  return AllOf(Property(&ScoreAdsRequest::ScoreAdsRawRequest::log_context,
                        EqLogContext(raw_request.log_context())));
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyLogContextPropagates) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         *key_fetcher_manager,
                         /* *crypto_client = */ nullptr,
                         std::move(async_reporter)};

  // Setup expectation on buyer client to receive appropriate log context from
  // SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  auto* expected_log_context = expected_get_bid_request.mutable_log_context();
  expected_log_context->set_generation_id(kSampleGenerationId);
  expected_log_context->set_adtech_debug_id(kSampleBuyerDebugId);

  auto MockGetBids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(kSampleBidValue);
        bid->set_interest_group_name(kSampleBuyer);
        std::move(on_done)(std::move(get_bids_response),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      };
  auto SetupMockBuyer =
      [&expected_get_bid_request,
       &MockGetBids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer,
                    ExecuteInternal(Pointee(EqGetBidsRawRequestWithLogContext(
                                        expected_get_bid_request)),
                                    _, _, _, _))
            .WillRepeatedly(MockGetBids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [SetupMockBuyer](absl::string_view hostname) {
    return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  // Setting up simple scoring signals provider so that the call flow can
  // proceed to Ad Scoring.
  auto MockScoringSignalsProvider =
      [](const ScoringSignalsRequest& scoring_signals_request,
         ScoringSignalsDoneCallback on_done, absl::Duration timeout,
         RequestContext context) {
        auto scoring_signals = std::make_unique<ScoringSignals>();
        scoring_signals->scoring_signals =
            std::make_unique<std::string>(kSampleScoringSignals);
        GetByteSize get_byte_size;
        std::move(on_done)(std::move(scoring_signals), get_byte_size);
      };
  EXPECT_CALL(scoring_signals_provider, Get)
      .WillRepeatedly(MockScoringSignalsProvider);

  // Setup expectation on scoring client to receive appropriate log context from
  // SFE.
  ScoreAdsRequest::ScoreAdsRawRequest expected_score_ads_request;
  auto* score_ads_log_context =
      expected_score_ads_request.mutable_log_context();
  score_ads_log_context->set_generation_id(kSampleGenerationId);
  score_ads_log_context->set_adtech_debug_id(kSampleSellerDebugId);
  auto mock_scoring_client_exec =
      [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
         grpc::ClientContext* context,
         absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                     ScoreAdsResponse::ScoreAdsRawResponse>>,
                                 ResponseMetadata)&&>
             on_done,
         absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
        return absl::OkStatus();
      };
  EXPECT_CALL(scoring_client,
              ExecuteInternal(Pointee(EqScoreAdsRawRequestWithLogContext(
                                  expected_score_ads_request)),
                              _, _, _, _))
      .WillRepeatedly(mock_scoring_client_exec);

  // Set log context that should be propagated to the downstream services.
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<TypeParam>(CLIENT_TYPE_BROWSER,
                                          kSellerOriginDomain);
  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);

  MockEntriesCallOnBuyerFactory(protected_auction_input.buyer_input(),
                                buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request);
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyBadInputGetsValidated) {
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
          CLIENT_TYPE_BROWSER, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/false);

  // Setup bad request that should be validated by our validation logic.
  auto& protected_auction_input = request_with_context.protected_auction_input;
  protected_auction_input.clear_generation_id();
  protected_auction_input.clear_publisher_name();

  google::protobuf::Map<std::string, BuyerInput> buyer_input_map;
  // A Buyer input with IGs.
  BuyerInput input_with_igs;
  // We don't validate anything about IGs.
  input_with_igs.mutable_interest_groups()->Add();
  buyer_input_map.emplace(kSampleBuyer, input_with_igs);

  // A Buyer input with no IGs is fine as long as there is at least one buyer
  // in the buyer input map with IGs.
  BuyerInput input_with_no_igs;
  buyer_input_map.emplace(kSampleBuyer2, input_with_no_igs);

  // Malformed buyer input (empty interest group owner name).
  BuyerInput ok_buyer_input;
  auto* ok_interest_groups = ok_buyer_input.mutable_interest_groups();
  auto* ok_interest_group = ok_interest_groups->Add();
  ok_interest_group->set_name(kSampleInterestGroupName);
  buyer_input_map.emplace(kEmptyBuyer, ok_buyer_input);

  auto encoded_buyer_inputs = GetEncodedBuyerInputMap(buyer_input_map);
  ASSERT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);

  // Set up the encoded cipher text in the request.
  auto [encrypted_request, context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  this->SetProtectedAuctionCipherText(protected_auction_input,
                                      encrypted_request,
                                      request_with_context.select_ad_request);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(response_with_cbor.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(), context,
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
  ASSERT_TRUE(decompressed_response.ok()) << decompressed_response.status();
  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  ASSERT_TRUE(deserialized_auction_result.ok())
      << deserialized_auction_result.status();

  std::vector<std::string> expected_errors = {kMissingGenerationId,
                                              kMissingPublisherName};
  EXPECT_EQ(deserialized_auction_result->error().message(),
            absl::StrJoin(expected_errors, kErrorDelimiter))
      << deserialized_auction_result->DebugString();
  EXPECT_EQ(deserialized_auction_result->error().code(),
            static_cast<int>(ErrorCode::CLIENT_SIDE));
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyNoBuyerInputsIsAnError) {
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
          CLIENT_TYPE_BROWSER, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/false);

  // Setup bad request that should be validated by our validation logic.
  auto& protected_auction_input = request_with_context.protected_auction_input;
  protected_auction_input.clear_buyer_input();

  // Set up the encoded cipher text in the request.
  auto [encrypted_request, context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  this->SetProtectedAuctionCipherText(protected_auction_input,
                                      encrypted_request,
                                      request_with_context.select_ad_request);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(response_with_cbor.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(), context,
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
  ASSERT_TRUE(decompressed_response.ok()) << decompressed_response.status();
  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  ASSERT_TRUE(deserialized_auction_result.ok())
      << deserialized_auction_result.status();

  EXPECT_EQ(deserialized_auction_result->error().message(), kMissingBuyerInputs)
      << deserialized_auction_result->DebugString();
  EXPECT_EQ(deserialized_auction_result->error().code(),
            static_cast<int>(ErrorCode::CLIENT_SIDE));

  // Validate chaff bit is not set if there was an input validation error.
  EXPECT_FALSE(deserialized_auction_result->is_chaff());
}

TYPED_TEST(SelectAdReactorForWebTest,
           VerifyANonEmptyYetMalformedBuyerInputMapIsCaught) {
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
          CLIENT_TYPE_BROWSER, kZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/false);

  // Set up a buyer input map with no usable buyer/IGs that could be used to get
  // a bid and expect that this is reported as an error.
  google::protobuf::Map<std::string, BuyerInput> buyer_input_map;
  BuyerInput input_with_igs;
  input_with_igs.mutable_interest_groups()->Add();
  buyer_input_map.emplace(kEmptyBuyer, input_with_igs);
  auto encoded_buyer_inputs = GetEncodedBuyerInputMap(buyer_input_map);
  ASSERT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();
  auto& protected_auction_input = request_with_context.protected_auction_input;
  *protected_auction_input.mutable_buyer_input() =
      *std::move(encoded_buyer_inputs);

  // Set up the encoded cipher text in the request.
  auto [encrypted_request, context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
  this->SetProtectedAuctionCipherText(protected_auction_input,
                                      encrypted_request,
                                      request_with_context.select_ad_request);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  EXPECT_FALSE(response_with_cbor.auction_result_ciphertext().empty());

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(), context,
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
  ASSERT_TRUE(decompressed_response.ok()) << decompressed_response.status();
  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  ASSERT_TRUE(deserialized_auction_result.ok())
      << deserialized_auction_result.status();
  std::string expected_error =
      absl::StrFormat(kNonEmptyBuyerInputMalformed, kEmptyInterestGroupOwner);
  EXPECT_EQ(deserialized_auction_result->error().message(), expected_error)
      << deserialized_auction_result->DebugString();
  EXPECT_EQ(deserialized_auction_result->error().code(),
            static_cast<int>(ErrorCode::CLIENT_SIDE));
}

auto EqConsentedDebugConfig(
    const server_common::ConsentedDebugConfiguration& consented_debug_config) {
  return AllOf(
      Property(&server_common::ConsentedDebugConfiguration::is_consented,
               Eq(consented_debug_config.is_consented())),
      Property(&server_common::ConsentedDebugConfiguration::token,
               Eq(consented_debug_config.token())));
}

auto EqGetBidsRawRequestWithConsentedDebugConfig(
    const GetBidsRequest::GetBidsRawRequest& raw_request) {
  return AllOf(
      Property(&GetBidsRequest::GetBidsRawRequest::consented_debug_config,
               EqConsentedDebugConfig(raw_request.consented_debug_config())));
}

auto EqScoreAdsRawRequestWithConsentedDebugConfig(
    const ScoreAdsRequest::ScoreAdsRawRequest& raw_request) {
  return AllOf(
      Property(&ScoreAdsRequest::ScoreAdsRawRequest::consented_debug_config,
               EqConsentedDebugConfig(raw_request.consented_debug_config())));
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyConsentedDebugConfigPropagates) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         *key_fetcher_manager,
                         /* *crypto_client = */ nullptr,
                         std::move(async_reporter)};

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  auto* expected_consented_debug_config =
      expected_get_bid_request.mutable_consented_debug_config();
  expected_consented_debug_config->set_is_consented(kIsConsentedDebug);
  expected_consented_debug_config->set_token(kConsentedDebugToken);

  auto MockGetBids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(kSampleBidValue);
        bid->set_interest_group_name(kSampleBuyer);
        std::move(on_done)(std::move(get_bids_response),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      };
  auto SetupMockBuyer =
      [&expected_get_bid_request,
       &MockGetBids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(
            *buyer,
            ExecuteInternal(Pointee(EqGetBidsRawRequestWithConsentedDebugConfig(
                                expected_get_bid_request)),
                            _, _, _, _))
            .WillRepeatedly(MockGetBids);
        return buyer;
      };
  auto MockBuyerFactoryCall = [SetupMockBuyer](absl::string_view hostname) {
    return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get(_))
      .WillRepeatedly(MockBuyerFactoryCall);

  // Setting up simple scoring signals provider so that the call flow can
  // proceed to Ad Scoring.
  auto MockScoringSignalsProvider =
      [](const ScoringSignalsRequest& scoring_signals_request,
         ScoringSignalsDoneCallback on_done, absl::Duration timeout,
         RequestContext context) {
        auto scoring_signals = std::make_unique<ScoringSignals>();
        scoring_signals->scoring_signals =
            std::make_unique<std::string>(kSampleScoringSignals);
        GetByteSize get_byte_size;
        std::move(on_done)(std::move(scoring_signals), get_byte_size);
      };
  EXPECT_CALL(scoring_signals_provider, Get)
      .WillRepeatedly(MockScoringSignalsProvider);

  // Setup expectation on scoring client from SFE.
  ScoreAdsRequest::ScoreAdsRawRequest expected_score_ads_request;
  auto* expected_consented_debug_config_for_score_ads =
      expected_score_ads_request.mutable_consented_debug_config();
  expected_consented_debug_config_for_score_ads->set_is_consented(
      kIsConsentedDebug);
  expected_consented_debug_config_for_score_ads->set_token(
      kConsentedDebugToken);
  auto mock_scoring_client_exec =
      [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
         grpc::ClientContext* context,
         absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                     ScoreAdsResponse::ScoreAdsRawResponse>>,
                                 ResponseMetadata)&&>
             on_done,
         absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
        return absl::OkStatus();
      };
  EXPECT_CALL(
      scoring_client,
      ExecuteInternal(Pointee(EqScoreAdsRawRequestWithConsentedDebugConfig(
                          expected_score_ads_request)),
                      _, _, _, _))
      .WillRepeatedly(mock_scoring_client_exec);

  // Set consented debug config that should be propagated to the downstream
  // services.
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<TypeParam>(CLIENT_TYPE_BROWSER,
                                          kSellerOriginDomain,
                                          /*is_consented_debug=*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);

  MockEntriesCallOnBuyerFactory(protected_auction_input.buyer_input(),
                                buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request);
}

TYPED_TEST(SelectAdReactorForWebTest,
           VerifyDeviceComponentAuctionCborEncoding) {
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
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true,
          kTestTopLevelSellerOriginDomain);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  EXPECT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response->size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  EXPECT_TRUE(decompressed_response.ok());

  std::string base64_response;
  absl::Base64Escape(*decompressed_response, &base64_response);
  ABSL_LOG(INFO)
      << "Decrypted, decompressed but CBOR encoded auction result :\n "
      << base64_response;

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok());
  EXPECT_FALSE(deserialized_auction_result->is_chaff());

  ABSL_LOG(INFO)
      << "Decrypted, decompressed and CBOR decoded auction result :\n "
      << MessageToJson(*deserialized_auction_result);

  // Validate that the bidding groups data is present.
  EXPECT_EQ(deserialized_auction_result->bidding_groups().size(), 1);
  const auto& [observed_buyer, interest_groups] =
      *deserialized_auction_result->bidding_groups().begin();
  EXPECT_EQ(observed_buyer, kSampleBuyer);
  EXPECT_EQ(deserialized_auction_result->top_level_seller(),
            kTestTopLevelSellerOriginDomain);
  std::set<int> observed_interest_group_indices(interest_groups.index().begin(),
                                                interest_groups.index().end());
  std::set<int> expected_interest_group_indices = {0};
  std::set<int> unexpected_interest_group_indices;
  absl::c_set_difference(
      observed_interest_group_indices, expected_interest_group_indices,
      std::inserter(unexpected_interest_group_indices,
                    unexpected_interest_group_indices.begin()));
  EXPECT_TRUE(unexpected_interest_group_indices.empty());
}

TYPED_TEST(SelectAdReactorForWebTest, FailsEncodingWhenModifiedBidIsZero) {
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
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, kTestTopLevelSellerOriginDomain,
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/true);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  EXPECT_FALSE(decrypted_response.ok()) << decrypted_response.status();
}

TYPED_TEST(SelectAdReactorForWebTest,
           VerifyServerComponentAuctionProtoEncoding) {
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
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, scoring_signals_provider,
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

  SelectAdResponse response_with_proto =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_proto);
  ASSERT_FALSE(response_with_proto.auction_result_ciphertext().empty());

  // Decrypt the response.
  absl::string_view decrypted_response =
      response_with_proto.auction_result_ciphertext();

  // Expect the payload to be of length that is a power of 2.
  const size_t payload_size = decrypted_response.size();
  int log_2_payload = log2(payload_size);
  EXPECT_EQ(payload_size, 1 << log_2_payload);
  EXPECT_GE(payload_size, kMinAuctionResultBytes);

  // Decompress the encoded response.
  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(decrypted_response);
  EXPECT_TRUE(decompressed_response.ok());

  // Validate the error message returned in the response.
  AuctionResult deserialized_auction_result;
  EXPECT_TRUE(deserialized_auction_result.ParseFromArray(
      decompressed_response->data(), decompressed_response->size()));

  // Validate chaff bit is not set if there was an input validation error.
  EXPECT_FALSE(deserialized_auction_result.is_chaff());
  EXPECT_FALSE(deserialized_auction_result.has_error());

  // Validate server component auction fields.
  EXPECT_EQ(deserialized_auction_result.auction_params().component_seller(),
            kSellerOriginDomain);
  EXPECT_EQ(
      deserialized_auction_result.auction_params().ciphertext_generation_id(),
      kSampleGenerationId);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
