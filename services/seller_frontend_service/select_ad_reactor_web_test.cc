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
#include "absl/strings/str_join.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "services/common/clients/k_anon_server/k_anon_client.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/hash_util.h"
#include "services/common/util/oblivious_http_utils.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_mock.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kSampleBidValue = 10.0;
constexpr char kSampleScoringSignals[] = "sampleScoringSignals";
constexpr KAnonCacheManagerConfig kKAnonCacheManagerConfig = {
    .total_num_hash = 3,
    .client_time_out = absl::Minutes(1),
    .k_anon_ttl = absl::Hours(24),
    .non_k_anon_ttl = absl::Hours(24)};

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
using ScoreAdsDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
         ResponseMetadata) &&>;
using google::scp::core::test::EqualsProto;

template <typename T, bool UseKvV2ForBrowser>
struct TypeDefinitions {
  typedef T InputType;
  static constexpr bool kUseKvV2ForBrowser = UseKvV2ForBrowser;
};

template <typename T>
class SelectAdReactorForWebTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // initialize
    privacy_sandbox::bidding_auction_servers::CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
    config_ = CreateConfig();
    config_.SetOverride("", CONSENTED_DEBUG_TOKEN);
    config_.SetOverride(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
    config_.SetOverride(kFalse, ENABLE_TKV_V2_BROWSER);
    if (T::kUseKvV2ForBrowser) {
      config_.SetOverride(kTrue, ENABLE_TKV_V2_BROWSER);
    }
    config_.SetOverride(kFalse, ENABLE_CHAFFING);
    config_.SetOverride("0", DEBUG_SAMPLE_RATE_MICRO);
    config_.SetOverride(kFalse, CONSENT_ALL_REQUESTS);
    config_.SetOverride("", K_ANON_API_KEY);
    config_.SetOverride(kFalse, TEST_MODE);
    config_.SetOverride("dns:///kv-v2-host", TRUSTED_KEY_VALUE_V2_SIGNALS_HOST);
    config_.SetOverride(kSignalsRequired, SCORING_SIGNALS_FETCH_MODE);
    config_.SetOverride("", HEADER_PASSED_TO_BUYER);

    EXPECT_CALL(*this->executor_, Run)
        .WillRepeatedly([](absl::AnyInvocable<void()> closure) { closure(); });
  }

  void SetProtectedAuctionCipherText(
      const typename T::InputType& protected_auction_input,
      const std::string& ciphertext, SelectAdRequest& select_ad_request) {
    const auto* descriptor = protected_auction_input.GetDescriptor();
    if (descriptor->name() == kProtectedAuctionInput) {
      *select_ad_request.mutable_protected_auction_ciphertext() = ciphertext;
    } else {
      *select_ad_request.mutable_protected_audience_ciphertext() = ciphertext;
    }
  }

  TrustedServersConfigClient config_ = TrustedServersConfigClient({});
  const HpkeKeyset default_keyset_ = HpkeKeyset{};
  std::unique_ptr<MockExecutor> executor_ = std::make_unique<MockExecutor>();
  std::unique_ptr<KAnonGrpcClient> k_anon_grpc_client_ =
      std::make_unique<KAnonGrpcClient>(
          KAnonClientConfig{.ca_root_pem = kTestCaCertPath});
};

using ProtectedAuctionInputTypes =
    ::testing::Types<TypeDefinitions<ProtectedAudienceInput, false>,
                     TypeDefinitions<ProtectedAudienceInput, true>,
                     TypeDefinitions<ProtectedAuctionInput, false>,
                     TypeDefinitions<ProtectedAuctionInput, true>>;
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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());

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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Setup expectation on buyer client to receive appropriate log context from
  // SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  auto* expected_log_context = expected_get_bid_request.mutable_log_context();
  expected_log_context->set_generation_id(kSampleGenerationId);
  expected_log_context->set_adtech_debug_id(kSampleBuyerDebugId);

  auto test_get_bids_response = []() {
    auto get_bids_response =
        std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    auto* bid = get_bids_response->mutable_bids()->Add();
    bid->set_bid(kSampleBidValue);
    bid->set_interest_group_name(kSampleBuyer);
    bid->set_render("someKey");  // Needs to match with the scoring signals.
    return get_bids_response;
  };
  expected_buyer_bids.try_emplace(std::string(kSampleBuyer),
                                  test_get_bids_response());
  auto MockGetBids =
      [test_get_bids_response](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(test_get_bids_response(),
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

  if (TypeParam::kUseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        kKvV2CompressionGroup, &kv_response));
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids,
                           {.repeated_get_allowed = true});
  } else {
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
  }

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
      GetSampleSelectAdRequest<typename TypeParam::InputType>(
          CLIENT_TYPE_BROWSER, kSellerOriginDomain);
  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);

  MockEntriesCallOnBuyerFactory(protected_auction_input.buyer_input(),
                                buyer_front_end_async_client_factory_mock);
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         &kv_async_client,
                         *key_fetcher_manager,
                         /* *crypto_client = */ nullptr,
                         std::move(async_reporter)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request,
                                               this->executor_.get());
}

TYPED_TEST(SelectAdReactorForWebTest, VerifyBadInputGetsValidated) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/false);

  // Setup bad request that should be validated by our validation logic.
  auto& protected_auction_input = request_with_context.protected_auction_input;
  protected_auction_input.clear_generation_id();
  protected_auction_input.clear_publisher_name();

  google::protobuf::Map<std::string, BuyerInputForBidding> buyer_input_map;
  // A Buyer input with IGs.
  BuyerInputForBidding input_with_igs;
  // We don't validate anything about IGs.
  input_with_igs.mutable_interest_groups()->Add();
  buyer_input_map.emplace(kSampleBuyer, input_with_igs);

  // A Buyer input with no IGs is fine as long as there is at least one buyer
  // in the buyer input map with IGs.
  BuyerInputForBidding input_with_no_igs;
  buyer_input_map.emplace(kSampleBuyer2, input_with_no_igs);

  // Malformed buyer input (empty interest group owner name).
  BuyerInputForBidding ok_buyer_input;
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
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
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
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/false);

  // Set up a buyer input map with no usable buyer/IGs that could be used to get
  // a bid and expect that this is reported as an error.
  google::protobuf::Map<std::string, BuyerInputForBidding> buyer_input_map;
  BuyerInputForBidding input_with_igs;
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
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         &kv_async_client,
                         *key_fetcher_manager,
                         /* *crypto_client = */ nullptr,
                         std::move(async_reporter)};

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  auto* expected_consented_debug_config =
      expected_get_bid_request.mutable_consented_debug_config();
  expected_consented_debug_config->set_is_consented(kIsConsentedDebug);
  expected_consented_debug_config->set_token(kConsentedDebugToken);

  auto test_get_bids_response = []() {
    auto get_bids_response =
        std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    auto* bid = get_bids_response->mutable_bids()->Add();
    bid->set_bid(kSampleBidValue);
    bid->set_interest_group_name(kSampleBuyer);
    bid->set_render("someKey");  // Needs to match with the scoring signals.
    return get_bids_response;
  };
  expected_buyer_bids.try_emplace(std::string(kSampleBuyer),
                                  test_get_bids_response());
  auto MockGetBids =
      [test_get_bids_response](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        std::move(on_done)(test_get_bids_response(),
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

  if (TypeParam::kUseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        kKvV2CompressionGroup, &kv_response));
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids,
                           {.repeated_get_allowed = true});
  } else {
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
  }

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
      GetSampleSelectAdRequest<typename TypeParam::InputType>(
          CLIENT_TYPE_BROWSER, kSellerOriginDomain,
          /*is_consented_debug=*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);

  MockEntriesCallOnBuyerFactory(protected_auction_input.buyer_input(),
                                buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request,
                                               this->executor_.get());
}

TYPED_TEST(SelectAdReactorForWebTest,
           VerifyDeviceComponentAuctionCborEncoding) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true,
          kTestTopLevelSellerOriginDomain);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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
  KVAsyncClientMock kv_async_client;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));

  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, kTestTopLevelSellerOriginDomain,
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/true);

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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
  KVAsyncClientMock kv_async_client;
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
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
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
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get());
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

TYPED_TEST(SelectAdReactorForWebTest, VerifyPopulatedKAnonAuctionResultData) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  // Create test AdScores for k-anon ghost winners.
  ScoreAdsResponse::AdScore ghost_score_1;
  ghost_score_1.set_render(absl::StrCat(kSampleBuyer2, kTestRenderUrlSuffix));
  for (int i = 0; i < 2; i++) {
    *ghost_score_1.mutable_component_renders()->Add() =
        absl::StrCat(kSampleBuyer2, kTestComponentUrlSuffix, i);
  }
  ghost_score_1.set_interest_group_name(kSampleInterestGroupName);
  ghost_score_1.set_interest_group_owner(kSampleBuyer2);
  ghost_score_1.set_buyer_reporting_id(kSampleBuyerReportingId);
  ghost_score_1.set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);
  ScoreAdsResponse::AdScore ghost_score_2;
  ghost_score_2.set_render(absl::StrCat(kSampleBuyer3, kTestRenderUrlSuffix));
  for (int i = 0; i < 2; i++) {
    *ghost_score_2.mutable_component_renders()->Add() =
        absl::StrCat(kSampleBuyer3, kTestComponentUrlSuffix, i);
  }
  ghost_score_2.set_interest_group_name(kSampleInterestGroupName);
  ghost_score_2.set_interest_group_owner(kSampleBuyer3);
  ghost_score_2.set_buyer_reporting_id(kSampleBuyerReportingId);
  ghost_score_2.set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  auto mock_are_k_anonymous =
      [](absl::string_view type,
         absl::flat_hash_set<absl::string_view> hash_sets,
         KAnonCacheManagerInterface::Callback on_done,
         metric::SfeContext* sfe_context) -> absl::Status {
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, /*top_level_seller=*/"",
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          {std::move(ghost_score_1), std::move(ghost_score_2)},
          std::move(k_anon_cache_manager));

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  buyer_report_win_js_urls.try_emplace(kSampleBuyer2, kSampleBiddingUrl2);
  buyer_report_win_js_urls.try_emplace(kSampleBuyer3, kSampleBiddingUrl3);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false,
          /*report_win_map=*/test_report_win_map);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
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
  EXPECT_TRUE(decompressed_response.ok());

  std::string base64_response;
  absl::Base64Escape(*decompressed_response, &base64_response);
  ABSL_LOG(INFO) << "Decrypted, decompressed but CBOR encoded auction result:\n"
                 << absl::BytesToHexString(*decompressed_response);

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok());
  EXPECT_FALSE(deserialized_auction_result->is_chaff());

  ABSL_LOG(INFO) << "Decrypted, decompressed and CBOR decoded auction result:\n"
                 << MessageToJson(*deserialized_auction_result);

  // Validate that k-anon winner join candidate data is present in
  // AuctionResult.
  HashUtil hasher = HashUtil();
  ASSERT_TRUE(deserialized_auction_result->has_k_anon_winner_join_candidates());
  auto winner_candidate =
      deserialized_auction_result->k_anon_winner_join_candidates();
  EXPECT_EQ(
      winner_candidate.ad_render_url_hash(),
      hasher.HashedKAnonKeyForAdRenderURL(
          /*owner=*/kSampleBuyer, /*bidding_url=*/kSampleBiddingUrl,
          /*render_url=*/absl::StrCat(kSampleBuyer, kTestRenderUrlSuffix)));
  EXPECT_GT(winner_candidate.ad_component_render_urls_hash().size(), 0);
  for (int i = 0; i < winner_candidate.ad_component_render_urls_hash().size();
       i++) {
    EXPECT_EQ(winner_candidate.ad_component_render_urls_hash().at(i),
              hasher.HashedKAnonKeyForAdComponentRenderURL(
                  absl::StrCat("https://fooAds.com/adComponents?id=", i)));
  }
  EXPECT_EQ(winner_candidate.reporting_id_hash(),
            hasher.HashedKAnonKeyForReportingID(
                /*owner=*/kSampleBuyer,
                /*ig_name*/ kSampleInterestGroupName,
                /*bidding_url*/ kSampleBiddingUrl,
                /*render_url*/ absl::StrCat(kSampleBuyer, kTestRenderUrlSuffix),
                /*reporting_ids*/ {}));

  // Validate that ghost winner data is present in AuctionResult.
  ASSERT_GT(deserialized_auction_result->k_anon_ghost_winners().size(), 0);
  auto ghost_winners = deserialized_auction_result->k_anon_ghost_winners();
  EXPECT_GT(ghost_winners.size(), 0);
  KAnonKeyReportingIDParam id_param = {
      .buyer_reporting_id = kSampleBuyerReportingId,
      .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId};
  for (int i = 0; i < ghost_winners.size(); i++) {
    EXPECT_EQ(ghost_winners.at(i).ig_name(), kSampleInterestGroupName);
    ASSERT_TRUE(ghost_winners.at(i).has_k_anon_join_candidates());
    auto ghost_winner_candidate = ghost_winners.at(i).k_anon_join_candidates();
    auto ghost_winner_component_hash =
        ghost_winner_candidate.ad_component_render_urls_hash();
    if (i == 0) {
      EXPECT_EQ(ghost_winners.at(i).owner(), kSampleBuyer2);
      EXPECT_EQ(ghost_winner_candidate.ad_render_url_hash(),
                hasher.HashedKAnonKeyForAdRenderURL(
                    /*owner=*/kSampleBuyer2, /*bidding_url=*/kSampleBiddingUrl2,
                    /*render_url=*/
                    absl::StrCat(kSampleBuyer2, kTestRenderUrlSuffix)));
      EXPECT_GT(ghost_winner_candidate.ad_component_render_urls_hash().size(),
                0);
      for (int j = 0; j < ghost_winner_component_hash.size(); j++) {
        EXPECT_EQ(ghost_winner_component_hash.at(j),
                  hasher.HashedKAnonKeyForAdComponentRenderURL(
                      absl::StrCat(kSampleBuyer2, kTestComponentUrlSuffix, j)));
      }
      EXPECT_EQ(
          ghost_winner_candidate.reporting_id_hash(),
          hasher.HashedKAnonKeyForReportingID(
              /*owner=*/kSampleBuyer2, /*ig_name*/ kSampleInterestGroupName,
              /*bidding_url*/ kSampleBiddingUrl2,
              /*render_url*/ absl::StrCat(kSampleBuyer2, kTestRenderUrlSuffix),
              /*reporting_ids*/ id_param));
    } else if (i == 1) {
      EXPECT_EQ(ghost_winners.at(i).owner(), kSampleBuyer3);
      EXPECT_EQ(ghost_winner_candidate.ad_render_url_hash(),
                hasher.HashedKAnonKeyForAdRenderURL(
                    /*owner=*/kSampleBuyer3, /*bidding_url=*/kSampleBiddingUrl3,
                    /*render_url=*/
                    absl::StrCat(kSampleBuyer3, kTestRenderUrlSuffix)));

      for (int j = 0; j < ghost_winner_component_hash.size(); j++) {
        EXPECT_EQ(ghost_winner_component_hash.at(j),
                  hasher.HashedKAnonKeyForAdComponentRenderURL(
                      absl::StrCat(kSampleBuyer3, kTestComponentUrlSuffix, j)));
      }
      EXPECT_EQ(
          ghost_winner_candidate.reporting_id_hash(),
          hasher.HashedKAnonKeyForReportingID(
              /*owner=*/kSampleBuyer3, /*ig_name*/ kSampleInterestGroupName,
              /*bidding_url*/ kSampleBiddingUrl3,
              /*render_url*/ absl::StrCat(kSampleBuyer3, kTestRenderUrlSuffix),
              /*reporting_ids*/ id_param));
    }
  }
}

TYPED_TEST(SelectAdReactorForWebTest,
           PopulatesKAnonDataAfterServerOrchestratedComponentAuction) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  // Create test AdScores for k-anon ghost winners.
  ScoreAdsResponse::AdScore ghost_score_1;
  ghost_score_1.set_render(absl::StrCat(kSampleBuyer2, kTestRenderUrlSuffix));
  for (int i = 0; i < 2; i++) {
    *ghost_score_1.mutable_component_renders()->Add() =
        absl::StrCat(kSampleBuyer2, kTestComponentUrlSuffix, i);
  }
  ghost_score_1.set_interest_group_name(kSampleInterestGroupName);
  ghost_score_1.set_interest_group_owner(kSampleBuyer2);
  ghost_score_1.set_buyer_reporting_id(kSampleBuyerReportingId);
  ghost_score_1.set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);
  ghost_score_1.set_bid(1);
  ScoreAdsResponse::AdScore ghost_score_2;
  ghost_score_2.set_render(absl::StrCat(kSampleBuyer3, kTestRenderUrlSuffix));
  for (int i = 0; i < 2; i++) {
    *ghost_score_2.mutable_component_renders()->Add() =
        absl::StrCat(kSampleBuyer3, kTestComponentUrlSuffix, i);
  }
  ghost_score_2.set_interest_group_name(kSampleInterestGroupName);
  ghost_score_2.set_interest_group_owner(kSampleBuyer3);
  ghost_score_2.set_buyer_reporting_id(kSampleBuyerReportingId);
  ghost_score_2.set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);
  ghost_score_2.set_bid(2);

  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));

  EXPECT_CALL(*key_fetcher_manager, GetPublicKey)
      .WillOnce(Return(google::cmrt::sdk::public_key_service::v1::PublicKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  auto mock_are_k_anonymous =
      [](absl::string_view type,
         absl::flat_hash_set<absl::string_view> hash_sets,
         KAnonCacheManagerInterface::Callback on_done,
         metric::SfeContext* sfe_context) -> absl::Status {
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClient(crypto_client);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, kTestTopLevelSellerOriginDomain,
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          {&crypto_client,
           EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_GCP},
          /*enforce_kanon=*/true,
          {std::move(ghost_score_1), std::move(ghost_score_2)},
          std::move(k_anon_cache_manager));

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  buyer_report_win_js_urls.try_emplace(kSampleBuyer2, kSampleBiddingUrl2);
  buyer_report_win_js_urls.try_emplace(kSampleBuyer3, kSampleBiddingUrl3);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_proto =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false, /*report_win_map=*/test_report_win_map);
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
  ASSERT_TRUE(deserialized_auction_result.has_k_anon_winner_join_candidates())
      << deserialized_auction_result.DebugString();
  // Validate that ghost winner data is present in AuctionResult.
  ASSERT_GT(deserialized_auction_result.k_anon_ghost_winners().size(), 0);
  AuctionResult expected_auction_result;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url: "https://ad_tech_A.com/ad"
        ad_component_render_urls: "https://fooAds.com/adComponents?id=0"
        ad_component_render_urls: "https://fooAds.com/adComponents?id=1"
        ad_component_render_urls: "https://fooAds.com/adComponents?id=2"
        interest_group_name: "interest_group"
        interest_group_owner: "https://ad_tech_A.com"
        score: 1
        bid: 1
        win_reporting_urls {
          buyer_reporting_urls {}
          component_seller_reporting_urls {}
          top_level_seller_reporting_urls {}
        }
        bidding_groups {
          key: "https://ad_tech_A.com"
          value { index: 0 }
        }
        top_level_seller: "top-level-seller.com"
        auction_params {
          ciphertext_generation_id: "6fa459ea-ee8a-3ca4-894e-db77e160355e"
          component_seller: "seller.com"
        }
        k_anon_winner_join_candidates {
          ad_render_url_hash: "A&v~gVy\205\367A8?\323(BU\234e\355\205\3275\t\010\334I\324]\202\3731\351"
          ad_component_render_urls_hash: "\211\003\215d \0141\003\020\366\234\375H\022\'\307\\c\317\336\361\220\334\354\244\357\025\243\217$\020)"
          ad_component_render_urls_hash: "\364\206\225\221\202\t\314\216\373\036\303o\3375Eq\002S\'\364nR\225\357\302e<\273|R\222\375"
          ad_component_render_urls_hash: "\247\364\302\316ir\2439TWb1\254\300\022\263\322\227>tey%1\217\355\3633\'B\336m"
          reporting_id_hash: "\247\n+P\3476\375g\267\340\236\362\276qn\224\300\301k\274Z8?\020\323\001\220\021\312\220[e"
        }
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\035?`\231\212\241m\321\255#\222\266~\301\323Q}\245\027\236L]]^\022l\005\346\tn\336P"
            ad_component_render_urls_hash: "\2400w\204\261\275\2211\213\300M\301\313\350[\266 \320\367\342\001lF\354\007&\335\223G\317\264\375"
            ad_component_render_urls_hash: "n\001\367;\312\213\177\000v\t\225n\372!N\374\246\353\322#\311\214\027\225\025\201W\014\016\013s/"
            reporting_id_hash: "\306\t\177\243a\216\325~\312x\2207$*\250\000\271\000\320\340NS\337VV\2641\357\037\367y\317"
          }
          owner: "https://ad_tech_B.com"
          ig_name: "interest_group"
          ghost_winner_for_top_level_auction {
            ad_render_url: "https://ad_tech_B.com/ad"
            ad_component_render_urls: "https://ad_tech_B.com/ad-component-0"
            ad_component_render_urls: "https://ad_tech_B.com/ad-component-1"
            modified_bid: 1
            buyer_reporting_id: "buyerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\213\336\200GY9\270(\376\233{%zO\376\345v\217\t\036\200)\361\302\224\223\373Wr\214\034i"
            ad_component_render_urls_hash: "\207\211\323\213\315\035=\353\207q\217\367\205S\320q]\030\032-A\244\327\222(M|\224\017\021\374\270"
            ad_component_render_urls_hash: "+=\343\275\262\222\213\025\236\364\355\367J\257\016.\332\'\265\366Y\363\025\365\335\007\371\200u/\177Z"
            reporting_id_hash: "\t]\331\214\226g]B\017\303\022\357\"\010\255\310\361\033\0041\327bY\201\322\274C\266\376\322\264&"
          }
          owner: "https://ad_tech_C.com"
          ig_name: "interest_group"
          ghost_winner_for_top_level_auction {
            ad_render_url: "https://ad_tech_C.com/ad"
            ad_component_render_urls: "https://ad_tech_C.com/ad-component-0"
            ad_component_render_urls: "https://ad_tech_C.com/ad-component-1"
            modified_bid: 2
            buyer_reporting_id: "buyerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
      )pb",
      &expected_auction_result));
  EXPECT_THAT(expected_auction_result,
              EqualsProto(deserialized_auction_result));
}

TYPED_TEST(SelectAdReactorForWebTest,
           PopulatesKAnonDataAfterDeviceOrchestratedComponentAuction) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  // Create test AdScores for k-anon ghost winners.
  ScoreAdsResponse::AdScore ghost_score_1;
  ghost_score_1.set_render(absl::StrCat(kSampleBuyer2, kTestRenderUrlSuffix));
  for (int i = 0; i < 2; i++) {
    *ghost_score_1.mutable_component_renders()->Add() =
        absl::StrCat(kSampleBuyer2, kTestComponentUrlSuffix, i);
  }
  ghost_score_1.set_interest_group_name(kSampleInterestGroupName);
  ghost_score_1.set_interest_group_owner(kSampleBuyer2);
  ghost_score_1.set_buyer_reporting_id(kSampleBuyerReportingId);
  ghost_score_1.set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);
  ghost_score_1.set_bid(1);
  ScoreAdsResponse::AdScore ghost_score_2;
  ghost_score_2.set_render(absl::StrCat(kSampleBuyer3, kTestRenderUrlSuffix));
  for (int i = 0; i < 2; i++) {
    *ghost_score_2.mutable_component_renders()->Add() =
        absl::StrCat(kSampleBuyer3, kTestComponentUrlSuffix, i);
  }
  ghost_score_2.set_interest_group_name(kSampleInterestGroupName);
  ghost_score_2.set_interest_group_owner(kSampleBuyer3);
  ghost_score_2.set_buyer_reporting_id(kSampleBuyerReportingId);
  ghost_score_2.set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);
  ghost_score_2.set_bid(2);

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  auto mock_are_k_anonymous =
      [](absl::string_view type,
         absl::flat_hash_set<absl::string_view> hash_sets,
         KAnonCacheManagerInterface::Callback on_done,
         metric::SfeContext* sfe_context) -> absl::Status {
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, kTestTopLevelSellerOriginDomain,
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          {std::move(ghost_score_1), std::move(ghost_score_2)},
          std::move(k_anon_cache_manager));

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  buyer_report_win_js_urls.try_emplace(kSampleBuyer2, kSampleBiddingUrl2);
  buyer_report_win_js_urls.try_emplace(kSampleBuyer3, kSampleBiddingUrl3);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false, /*report_win_map=*/test_report_win_map);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
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
  EXPECT_TRUE(decompressed_response.ok());

  std::string base64_response;
  absl::Base64Escape(*decompressed_response, &base64_response);
  ABSL_LOG(INFO) << "Decrypted, decompressed but CBOR encoded auction result:\n"
                 << absl::BytesToHexString(*decompressed_response);

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  ASSERT_TRUE(deserialized_auction_result.ok());
  EXPECT_FALSE(deserialized_auction_result->is_chaff());

  ABSL_LOG(INFO) << "Decrypted, decompressed and CBOR decoded auction result:\n"
                 << MessageToJson(*deserialized_auction_result);
  ASSERT_TRUE(deserialized_auction_result->has_k_anon_winner_join_candidates())
      << deserialized_auction_result->DebugString();
  // Validate that ghost winner data is present in AuctionResult.
  ASSERT_GT(deserialized_auction_result->k_anon_ghost_winners().size(), 0);
  AuctionResult expected_auction_result;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        ad_render_url: "https://ad_tech_A.com/ad"
        ad_component_render_urls: "https://fooAds.com/adComponents?id=0"
        ad_component_render_urls: "https://fooAds.com/adComponents?id=1"
        ad_component_render_urls: "https://fooAds.com/adComponents?id=2"
        interest_group_name: "interest_group"
        interest_group_owner: "https://ad_tech_A.com"
        score: 1
        bid: 1
        bidding_groups {
          key: "https://ad_tech_A.com"
          value { index: 0 }
        }
        top_level_seller: "top-level-seller.com"
        ad_metadata: "testAdMetadata"
        k_anon_winner_join_candidates {
          ad_render_url_hash: "A&v~gVy\205\367A8?\323(BU\234e\355\205\3275\t\010\334I\324]\202\3731\351"
          ad_component_render_urls_hash: "\211\003\215d \0141\003\020\366\234\375H\022\'\307\\c\317\336\361\220\334\354\244\357\025\243\217$\020)"
          ad_component_render_urls_hash: "\364\206\225\221\202\t\314\216\373\036\303o\3375Eq\002S\'\364nR\225\357\302e<\273|R\222\375"
          ad_component_render_urls_hash: "\247\364\302\316ir\2439TWb1\254\300\022\263\322\227>tey%1\217\355\3633\'B\336m"
          reporting_id_hash: "\247\n+P\3476\375g\267\340\236\362\276qn\224\300\301k\274Z8?\020\323\001\220\021\312\220[e"
        }
        k_anon_winner_positional_index: 0
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\035?`\231\212\241m\321\255#\222\266~\301\323Q}\245\027\236L]]^\022l\005\346\tn\336P"
            ad_component_render_urls_hash: "\2400w\204\261\275\2211\213\300M\301\313\350[\266 \320\367\342\001lF\354\007&\335\223G\317\264\375"
            ad_component_render_urls_hash: "n\001\367;\312\213\177\000v\t\225n\372!N\374\246\353\322#\311\214\027\225\025\201W\014\016\013s/"
            reporting_id_hash: "\306\t\177\243a\216\325~\312x\2207$*\250\000\271\000\320\340NS\337VV\2641\357\037\367y\317"
          }
          owner: "https://ad_tech_B.com"
          ig_name: "interest_group"
          ghost_winner_for_top_level_auction {
            ad_render_url: "https://ad_tech_B.com/ad"
            ad_component_render_urls: "https://ad_tech_B.com/ad-component-0"
            ad_component_render_urls: "https://ad_tech_B.com/ad-component-1"
            modified_bid: 1
            buyer_reporting_id: "buyerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\213\336\200GY9\270(\376\233{%zO\376\345v\217\t\036\200)\361\302\224\223\373Wr\214\034i"
            ad_component_render_urls_hash: "\207\211\323\213\315\035=\353\207q\217\367\205S\320q]\030\032-A\244\327\222(M|\224\017\021\374\270"
            ad_component_render_urls_hash: "+=\343\275\262\222\213\025\236\364\355\367J\257\016.\332\'\265\366Y\363\025\365\335\007\371\200u/\177Z"
            reporting_id_hash: "\t]\331\214\226g]B\017\303\022\357\"\010\255\310\361\033\0041\327bY\201\322\274C\266\376\322\264&"
          }
          owner: "https://ad_tech_C.com"
          ig_name: "interest_group"
          ghost_winner_for_top_level_auction {
            ad_render_url: "https://ad_tech_C.com/ad"
            ad_component_render_urls: "https://ad_tech_C.com/ad-component-0"
            ad_component_render_urls: "https://ad_tech_C.com/ad-component-1"
            modified_bid: 2
            buyer_reporting_id: "buyerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
      )pb",
      &expected_auction_result));
  EXPECT_THAT(expected_auction_result,
              EqualsProto(*deserialized_auction_result));
}

TYPED_TEST(SelectAdReactorForWebTest, QueriesAllRequiredHashes) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  HashUtil hasher = HashUtil();
  absl::flat_hash_set<std::string> expected_hashes_to_query = {
      hasher.HashedKAnonKeyForAdRenderURL(
          /*owner=*/kSampleBuyer, /*bidding_url=*/kSampleBiddingUrl,
          /*render_url=*/absl::StrCat(kSampleBuyer, kTestRenderUrlSuffix)),
      hasher.HashedKAnonKeyForReportingID(
          /*owner=*/kSampleBuyer,
          /*ig_name*/ kSampleInterestGroupName,
          /*bidding_url*/ kSampleBiddingUrl,
          /*render_url*/ absl::StrCat(kSampleBuyer, kTestRenderUrlSuffix),
          /*reporting_ids*/ {}),
  };
  for (int i = 0; i < 3; i++) {
    expected_hashes_to_query.insert(
        hasher.HashedKAnonKeyForAdComponentRenderURL(
            absl::StrCat("https://fooAds.com/adComponents?id=", i)));
  }
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  auto mock_are_k_anonymous =
      [expected_hashes_to_query](
          absl::string_view type,
          absl::flat_hash_set<absl::string_view> hash_sets,
          KAnonCacheManagerInterface::Callback on_done,
          metric::SfeContext* sfe_context) -> absl::Status {
    PS_VLOG(5) << " Observed hashes to query: "
               << absl::StrJoin(hash_sets, ", ",
                                [](std::string* out, absl::string_view hash) {
                                  out->append(absl::BytesToHexString(hash));
                                });
    absl::flat_hash_set<absl::string_view> missing_hashes;
    absl::flat_hash_set<absl::string_view> unexpected_hashes;
    for (auto observed_hash : hash_sets) {
      if (!expected_hashes_to_query.contains(observed_hash)) {
        unexpected_hashes.insert(observed_hash);
      }
    }
    for (const auto& expected_hash : expected_hashes_to_query) {
      if (!hash_sets.contains(expected_hash)) {
        missing_hashes.insert(expected_hash);
      }
    }
    EXPECT_TRUE(unexpected_hashes.empty())
        << "Unexpected hashes being queried from the k-anon cache manager: "
        << absl::StrJoin(unexpected_hashes, ", ",
                         [](std::string* out, absl::string_view hash) {
                           out->append(absl::BytesToHexString(hash));
                         });
    EXPECT_TRUE(missing_hashes.empty())
        << "Hashes unexpectedly not being queried from the k-anon cache "
           "manager: "
        << absl::StrJoin(unexpected_hashes, ", ",
                         [](std::string* out, absl::string_view hash) {
                           out->append(absl::BytesToHexString(hash));
                         });
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, /*top_level_seller=*/"",
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          /*kanon_ghost_winners=*/{}, std::move(k_anon_cache_manager));

  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false,
          /*report_win_map=*/test_report_win_map);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
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
  EXPECT_TRUE(decompressed_response.ok());

  std::string base64_response;
  absl::Base64Escape(*decompressed_response, &base64_response);
  ABSL_LOG(INFO) << "Decrypted, decompressed but CBOR encoded auction result:\n"
                 << absl::BytesToHexString(*decompressed_response);

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok());
  EXPECT_FALSE(deserialized_auction_result->is_chaff());

  ABSL_LOG(INFO) << "Decrypted, decompressed and CBOR decoded auction result:\n"
                 << MessageToJson(*deserialized_auction_result);

  // Validate that k-anon winner join candidate data is present in
  // AuctionResult.
  EXPECT_TRUE(deserialized_auction_result->has_k_anon_winner_join_candidates());
}

TYPED_TEST(SelectAdReactorForWebTest, SetsKAnonStatusOnScoringRequests) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  auto mock_are_k_anonymous =
      [](absl::string_view type,
         absl::flat_hash_set<absl::string_view> hash_sets,
         KAnonCacheManagerInterface::Callback on_done,
         metric::SfeContext* sfe_context) -> absl::Status {
    PS_VLOG(5) << " Observed hashes to query: "
               << absl::StrJoin(hash_sets, ", ",
                                [](std::string* out, absl::string_view hash) {
                                  out->append(absl::BytesToHexString(hash));
                                });
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, /*top_level_seller=*/"",
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          /*kanon_ghost_winners=*/{}, std::move(k_anon_cache_manager));

  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
             grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
             absl::Duration timeout, RequestConfig request_config) {
            for (const auto& bid : request->ad_bids()) {
              EXPECT_TRUE(bid.k_anon_status());
              auto response =
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
              ScoreAdsResponse::AdScore* score = response->mutable_ad_score();
              EXPECT_FALSE(bid.render().empty());
              score->set_render(bid.render());
              score->mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score->set_desirability(kNonZeroDesirability);
              score->set_buyer_bid(kNonZeroBidValue);
              score->set_interest_group_name(bid.interest_group_name());
              score->set_interest_group_owner(kSampleBuyer);
              std::move(on_done)(std::move(response),
                                 /* response_metadata= */ {});
              // Expect only one bid.
              break;
            }
            return absl::OkStatus();
          });
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false,
          /*report_win_map=*/test_report_win_map);
  ABSL_LOG(INFO) << "Encrypted SelectAdResponse:\n"
                 << MessageToJson(response_with_cbor);

  // Decrypt the response.
  auto decrypted_response = FromObliviousHTTPResponse(
      *response_with_cbor.mutable_auction_result_ciphertext(),
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
  EXPECT_TRUE(decompressed_response.ok());

  std::string base64_response;
  absl::Base64Escape(*decompressed_response, &base64_response);
  ABSL_LOG(INFO) << "Decrypted, decompressed but CBOR encoded auction result:\n"
                 << absl::BytesToHexString(*decompressed_response);

  absl::StatusOr<AuctionResult> deserialized_auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(deserialized_auction_result.ok());
  EXPECT_FALSE(deserialized_auction_result->is_chaff());

  ABSL_LOG(INFO) << "Decrypted, decompressed and CBOR decoded auction result:\n"
                 << MessageToJson(*deserialized_auction_result);

  // Validate that k-anon winner join candidate data is present in
  // AuctionResult.
  EXPECT_TRUE(deserialized_auction_result->has_k_anon_winner_join_candidates());
}

TYPED_TEST(SelectAdReactorForWebTest,
           AdIsConsideredNonKAnonIfRenderUrlHashIsNotKAnon) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  HashUtil hasher = HashUtil();
  std::string non_k_anon_hash = hasher.HashedKAnonKeyForAdRenderURL(
      /*owner=*/kSampleBuyer, /*bidding_url=*/kSampleBiddingUrl,
      /*render_url=*/absl::StrCat(kSampleBuyer, kTestRenderUrlSuffix));
  auto mock_are_k_anonymous =
      [non_k_anon_hash](absl::string_view type,
                        absl::flat_hash_set<absl::string_view> hash_sets,
                        KAnonCacheManagerInterface::Callback on_done,
                        metric::SfeContext* sfe_context) -> absl::Status {
    PS_VLOG(5) << " Observed hashes to query: "
               << absl::StrJoin(hash_sets, ", ",
                                [](std::string* out, absl::string_view hash) {
                                  out->append(absl::BytesToHexString(hash));
                                });
    EXPECT_TRUE(hash_sets.contains(non_k_anon_hash));
    hash_sets.erase(non_k_anon_hash);
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, /*top_level_seller=*/"",
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          /*kanon_ghost_winners=*/{}, std::move(k_anon_cache_manager));

  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
             grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
             absl::Duration timeout, RequestConfig request_config) {
            for (const auto& bid : request->ad_bids()) {
              EXPECT_FALSE(bid.k_anon_status());
              auto response =
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
              ScoreAdsResponse::AdScore* score = response->mutable_ad_score();
              EXPECT_FALSE(bid.render().empty());
              score->set_render(bid.render());
              score->mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score->set_desirability(kNonZeroDesirability);
              score->set_buyer_bid(kNonZeroBidValue);
              score->set_interest_group_name(bid.interest_group_name());
              score->set_interest_group_owner(kSampleBuyer);
              std::move(on_done)(std::move(response),
                                 /* response_metadata= */ {});
              // Expect only one bid.
              break;
            }
            return absl::OkStatus();
          });
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false,
          /*report_win_map=*/test_report_win_map);
}

TYPED_TEST(SelectAdReactorForWebTest,
           AdIsConsideredNonKAnonIfNameReportHashIsNotKAnon) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  HashUtil hasher = HashUtil();
  std::string non_k_anon_hash = hasher.HashedKAnonKeyForReportingID(
      /*owner=*/kSampleBuyer,
      /*ig_name*/ kSampleInterestGroupName,
      /*bidding_url*/ kSampleBiddingUrl,
      /*render_url*/ absl::StrCat(kSampleBuyer, kTestRenderUrlSuffix),
      /*reporting_ids*/ {});
  auto mock_are_k_anonymous =
      [non_k_anon_hash](absl::string_view type,
                        absl::flat_hash_set<absl::string_view> hash_sets,
                        KAnonCacheManagerInterface::Callback on_done,
                        metric::SfeContext* sfe_context) -> absl::Status {
    PS_VLOG(5) << " Observed hashes to query: "
               << absl::StrJoin(hash_sets, ", ",
                                [](std::string* out, absl::string_view hash) {
                                  out->append(absl::BytesToHexString(hash));
                                });
    EXPECT_TRUE(hash_sets.contains(non_k_anon_hash));
    hash_sets.erase(non_k_anon_hash);
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, /*top_level_seller=*/"",
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          /*kanon_ghost_winners=*/{}, std::move(k_anon_cache_manager));

  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
             grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
             absl::Duration timeout, RequestConfig request_config) {
            for (const auto& bid : request->ad_bids()) {
              EXPECT_FALSE(bid.k_anon_status());
              auto response =
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
              ScoreAdsResponse::AdScore* score = response->mutable_ad_score();
              EXPECT_FALSE(bid.render().empty());
              score->set_render(bid.render());
              score->mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score->set_desirability(kNonZeroDesirability);
              score->set_buyer_bid(kNonZeroBidValue);
              score->set_interest_group_name(bid.interest_group_name());
              score->set_interest_group_owner(kSampleBuyer);
              std::move(on_done)(std::move(response),
                                 /* response_metadata= */ {});
              // Expect only one bid.
              break;
            }
            return absl::OkStatus();
          });
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false,
          /*report_win_map=*/test_report_win_map);
}

TYPED_TEST(SelectAdReactorForWebTest,
           AdIsConsideredNonKAnonIfAComponentRenderHashIsNotKAnon) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();

  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  auto k_anon_cache_manager = std::make_unique<KAnonCacheManagerMock>(
      this->executor_.get(), std::move(this->k_anon_grpc_client_),
      kKAnonCacheManagerConfig);
  HashUtil hasher = HashUtil();
  std::string non_k_anon_hash = hasher.HashedKAnonKeyForAdComponentRenderURL(
      "https://fooAds.com/adComponents?id=2");
  auto mock_are_k_anonymous =
      [non_k_anon_hash](absl::string_view type,
                        absl::flat_hash_set<absl::string_view> hash_sets,
                        KAnonCacheManagerInterface::Callback on_done,
                        metric::SfeContext* sfe_context) -> absl::Status {
    PS_VLOG(5) << " Observed hashes to query: "
               << absl::StrJoin(hash_sets, ", ",
                                [](std::string* out, absl::string_view hash) {
                                  out->append(absl::BytesToHexString(hash));
                                });
    EXPECT_TRUE(hash_sets.contains(non_k_anon_hash));
    hash_sets.erase(non_k_anon_hash);
    std::move(on_done)(
        absl::flat_hash_set<std::string>(hash_sets.begin(), hash_sets.end()));
    return absl::OkStatus();
  };
  EXPECT_CALL(*k_anon_cache_manager, AreKAnonymous)
      .WillOnce(mock_are_k_anonymous);
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, kNonZeroBidValue, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain,
          /*expect_all_buyers_solicited=*/true, /*top_level_seller=*/"",
          /*enable_reporting=*/false,
          /*force_set_modified_bid_to_zero=*/false,
          /*server_component_auction_params=*/{}, /*enforce_kanon=*/true,
          /*kanon_ghost_winners=*/{}, std::move(k_anon_cache_manager));

  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
             grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
             absl::Duration timeout, RequestConfig request_config) {
            for (const auto& bid : request->ad_bids()) {
              EXPECT_FALSE(bid.k_anon_status());
              auto response =
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
              ScoreAdsResponse::AdScore* score = response->mutable_ad_score();
              EXPECT_FALSE(bid.render().empty());
              score->set_render(bid.render());
              score->mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score->set_desirability(kNonZeroDesirability);
              score->set_buyer_bid(kNonZeroBidValue);
              score->set_interest_group_name(bid.interest_group_name());
              score->set_interest_group_owner(kSampleBuyer);
              std::move(on_done)(std::move(response),
                                 /* response_metadata= */ {});
              // Expect only one bid.
              break;
            }
            return absl::OkStatus();
          });
  MockEntriesCallOnBuyerFactory(
      request_with_context.protected_auction_input.buyer_input(),
      buyer_front_end_async_client_factory_mock);

  absl::flat_hash_map<std::string, std::string> buyer_report_win_js_urls;
  buyer_report_win_js_urls.try_emplace(kSampleBuyer, kSampleBiddingUrl);
  ReportWinMap test_report_win_map = {.buyer_report_win_js_urls =
                                          std::move(buyer_report_win_js_urls)};
  SelectAdResponse response_with_cbor =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, request_with_context.select_ad_request,
          this->executor_.get(),
          /*enable_kanon=*/true,
          /*enable_buyer_private_aggregate_reporting=*/false,
          /*per_adtech_paapi_contributions_limit=*/100,
          /*fail_fast=*/false,
          /*report_win_map=*/test_report_win_map);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
