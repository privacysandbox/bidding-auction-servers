//  Copyright 2024 Google LLC
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

#include "services/auction_service/auction_service_integration_test_util.h"

#include <vector>

#include "absl/strings/str_format.h"
#include "google/protobuf/text_format.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;
using ::testing::AnyNumber;

constexpr absl::string_view kSecret = "secret";
constexpr absl::string_view kEurosIsoCode = "EUR";
constexpr absl::string_view kEgressPayload =
    "{\"features\": [{\"type\": \"boolean-feature\", \"value\": true}, "
    "{\"type\": \"unsigned-integer-feature\", \"value\": 127}]}";
constexpr absl::string_view kTemporaryUnlimitedEgressPayload =
    "{\"features\": [{\"type\": \"boolean-feature\", \"value\": "
    "true},{\"type\": \"unsigned-integer-feature\", \"value\": 2}]}";

AdWithBidMetadata GetTestAdWithBidMetadata(
    const TestScoreAdsRequestConfig& test_score_ads_request_config) {
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad;
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad.set_bid(1.0);
  ad.add_ad_components("adComponent.com");
  if (test_score_ads_request_config.buyer_reporting_id) {
    ad.set_buyer_reporting_id(
        *test_score_ads_request_config.buyer_reporting_id);
  }
  if (test_score_ads_request_config.buyer_and_seller_reporting_id) {
    ad.set_buyer_and_seller_reporting_id(
        *test_score_ads_request_config.buyer_and_seller_reporting_id);
  }
  if (test_score_ads_request_config.selected_buyer_and_seller_reporting_id) {
    ad.set_selected_buyer_and_seller_reporting_id(
        *test_score_ads_request_config.selected_buyer_and_seller_reporting_id);
  }
  ad.set_bid_currency(kEurosIsoCode);
  ad.set_ad_cost(
      test_score_ads_request_config.test_buyer_reporting_signals.ad_cost);
  ad.set_modeling_signals(test_score_ads_request_config
                              .test_buyer_reporting_signals.modeling_signals);
  ad.set_join_count(
      test_score_ads_request_config.test_buyer_reporting_signals.join_count);
  ad.set_interest_group_name(test_score_ads_request_config.interest_group_name);
  ad.set_interest_group_owner(
      test_score_ads_request_config.interest_group_owner);
  ad.set_render(absl::StrFormat(
      "%s/ads?id=%s", test_score_ads_request_config.interest_group_owner,
      MakeARandomString()));
  ad.set_recency(
      test_score_ads_request_config.test_buyer_reporting_signals.recency);
  ad.set_data_version(
      test_score_ads_request_config.test_buyer_reporting_signals.data_version);
  ad.set_interest_group_idx(kTestIgIdx);
  if (test_score_ads_request_config.k_anon_status) {
    ad.set_k_anon_status(*test_score_ads_request_config.k_anon_status);
  }
  return ad;
}

ProtectedAppSignalsAdWithBidMetadata GetTestAdWithBidMetadataForPAS(
    const TestScoreAdsRequestConfig& test_score_ads_request_config) {
  ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata ad;
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad.set_bid(1.0);
  ad.set_bid_currency(kEurosIsoCode);
  ad.set_ad_cost(
      test_score_ads_request_config.test_buyer_reporting_signals.ad_cost);
  ad.set_modeling_signals(test_score_ads_request_config
                              .test_buyer_reporting_signals.modeling_signals);
  ad.set_owner(test_score_ads_request_config.interest_group_name);
  ad.set_owner(test_score_ads_request_config.interest_group_owner);
  ad.set_render(absl::StrFormat(
      "%s/ads", test_score_ads_request_config.interest_group_owner));
  ad.set_egress_payload(kEgressPayload);
  ad.set_temporary_unlimited_egress_payload(kTemporaryUnlimitedEgressPayload);
  return ad;
}

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.

  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         absl::string_view ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        hpke_decrypt_response.set_payload(ciphertext);
        hpke_decrypt_response.set_secret(kSecret);
        return hpke_decrypt_response;
      });

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillRepeatedly(
          [](absl::string_view plaintext_payload, absl::string_view secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            data.set_ciphertext(plaintext_payload);
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });
}

void InitV8Dispatcher(V8Dispatcher& dispatcher) {
  ASSERT_TRUE(dispatcher.Init().ok());
}

void LoadWrapperWithMockSellerUdf(
    absl::string_view adtech_code_blob,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    V8Dispatcher& dispatcher) {
  std::string wrapper_js_blob = GetSellerWrappedCode(
      adtech_code_blob,
      auction_service_runtime_config.enable_report_result_url_generation,
      auction_service_runtime_config.enable_private_aggregate_reporting);
  ASSERT_TRUE(
      dispatcher
          .LoadSync(auction_service_runtime_config.default_score_ad_version,
                    std::move(wrapper_js_blob))
          .ok());
}

void LoadWrapperWithMockReportWinUdf(
    V8Dispatcher& dispatcher, const ScoreAdsRequest& request,
    absl::string_view buyer_udf,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    AuctionType auction_type) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  ASSERT_TRUE(raw_request.ParseFromString(request.request_ciphertext()));
  if (!auction_service_runtime_config.enable_report_win_url_generation) {
    return;
  } else if (buyer_udf.empty()) {
    PS_VLOG(kNoisyInfo) << "No buyer code loaded";
    return;
  }
  for (const auto& ad_bid : raw_request.ad_bids()) {
    std::string wrapper_js_blob = GetBuyerWrappedCode(
        buyer_udf, auction_service_runtime_config.enable_protected_app_signals,
        auction_service_runtime_config.enable_private_aggregate_reporting);
    absl::StatusOr<std::string> version =
        GetBuyerReportWinVersion(ad_bid.interest_group_owner(), auction_type);
    ASSERT_TRUE(version.ok());
    ASSERT_TRUE(dispatcher.LoadSync(*version, std::move(wrapper_js_blob)).ok());
  }
}

void LoadWrapperWithMockReportWinUdfForPAS(
    V8Dispatcher& dispatcher, const ScoreAdsRequest& request,
    absl::string_view buyer_udf,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    AuctionType auction_type) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  ASSERT_TRUE(raw_request.ParseFromString(request.request_ciphertext()));
  if (!auction_service_runtime_config.enable_report_win_url_generation) {
    return;
  } else if (buyer_udf.empty()) {
    PS_VLOG(kNoisyInfo) << "No buyer code loaded";
    return;
  }
  for (const auto& ad_bid : raw_request.protected_app_signals_ad_bids()) {
    std::string wrapper_js_blob = GetBuyerWrappedCode(buyer_udf);
    absl::StatusOr<std::string> version =
        GetBuyerReportWinVersion(ad_bid.owner(), auction_type);
    ASSERT_TRUE(version.ok());
    ASSERT_TRUE(dispatcher.LoadSync(*version, std::move(wrapper_js_blob)).ok());
  }
}

void RunTestScoreAds(
    V8DispatchClient& client, ScoreAdsRequest& request,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    ScoreAdsResponse& response) {
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto score_ads_reactor_factory =
      [&client, async_reporter_local = std::move(async_reporter)](
          grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            context, client, request, response,
            std::make_unique<ScoreAdsNoOpLogger>(), key_fetcher_manager,
            crypto_client, *async_reporter_local, runtime_config);
      };

  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);
  LocalAuctionStartResult result = StartLocalAuction(&service);
  std::unique_ptr<Auction::StubInterface> stub = CreateAuctionStub(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->ScoreAds(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << status.error_message();
}

// Loads Seller's udf containing scoreAd() and reportResult() as well as
// buyer's udf containing reportWin() for Protected Audience into Roma.
void LoadPABuyerAndSellerCode(const ScoreAdsRequest& request,
                              V8Dispatcher& dispatcher,
                              const AuctionServiceRuntimeConfig& runtime_config,
                              absl::string_view buyer_udf,
                              absl::string_view seller_udf) {
  InitV8Dispatcher(dispatcher);
  LoadWrapperWithMockSellerUdf(seller_udf, runtime_config, dispatcher);
  LoadWrapperWithMockReportWinUdf(dispatcher, request, buyer_udf,
                                  runtime_config,
                                  AuctionType::kProtectedAudience);
}

void LoadPASBuyerAndSellerCode(
    const ScoreAdsRequest& request, V8Dispatcher& dispatcher,
    const AuctionServiceRuntimeConfig& runtime_config,
    absl::string_view buyer_udf, absl::string_view seller_udf) {
  InitV8Dispatcher(dispatcher);
  LoadWrapperWithMockSellerUdf(seller_udf, runtime_config, dispatcher);
  LoadWrapperWithMockReportWinUdfForPAS(dispatcher, request, buyer_udf,
                                        runtime_config,
                                        AuctionType::kProtectedAppSignals);
}

}  // namespace

TestComponentAuctionResultData GenerateTestComponentAuctionResultData() {
  return {
      .test_component_seller = "",
      .generation_id = kTestGenerationId,
      .test_ig_owner = kTestIgOwner,
      .test_component_win_reporting_url = kExpectedComponentReportWinUrl,
      .test_component_report_result_url = kExpectedComponentReportWinUrl,
      .test_component_event = kTestInteractionEvent,
      .test_component_interaction_reporting_url = kTestInteractionReportingUrl};
}

ScoreAdsRequest BuildScoreAdsRequest(
    const TestScoreAdsRequestConfig& test_score_ads_request_config) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (int i = 0; i < test_score_ads_request_config.desired_ad_count; i++) {
    auto ad = GetTestAdWithBidMetadata(test_score_ads_request_config);
    if (!test_score_ads_request_config.test_buyer_reporting_signals
             .buyer_signals.empty()) {
      raw_request.mutable_per_buyer_signals()->try_emplace(
          ad.interest_group_owner(),
          test_score_ads_request_config.test_buyer_reporting_signals
              .buyer_signals);
    }
    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
    *raw_request.mutable_ad_bids()->Add() = ad;
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");
  if (test_score_ads_request_config.generate_scoring_signals) {
    raw_request.set_scoring_signals(trusted_scoring_signals);
  }
  raw_request.set_enable_debug_reporting(
      test_score_ads_request_config.enable_debug_reporting);
  raw_request.mutable_fdo_flags()->set_enable_sampled_debug_reporting(
      test_score_ads_request_config.enable_sampled_debug_reporting);
  raw_request.mutable_fdo_flags()->set_in_cooldown_or_lockout(
      test_score_ads_request_config.in_cooldown_or_lockout);
  raw_request.set_seller(test_score_ads_request_config.seller);
  // top_level_seller is expected to be set only for component auctions
  if (!test_score_ads_request_config.top_level_seller.empty()) {
    raw_request.set_top_level_seller(
        test_score_ads_request_config.top_level_seller);
  }
  raw_request.set_publisher_hostname(kPublisherHostname);
  raw_request.set_auction_signals(
      test_score_ads_request_config.test_buyer_reporting_signals
          .auction_signals);
  raw_request.mutable_consented_debug_config()->set_is_consented(
      test_score_ads_request_config.is_consented);
  raw_request.mutable_consented_debug_config()->set_token(kTestConsentToken);
  raw_request.set_seller_data_version(
      test_score_ads_request_config.seller_data_version);
  const auto& component_auction_data =
      test_score_ads_request_config.component_auction_data;
  // component_seller is expected to be set only for top level auctions.
  if (!component_auction_data.test_component_seller.empty()) {
    auto auction_result = MakeARandomComponentAuctionResultWithReportingUrls(
        component_auction_data);
    *raw_request.mutable_component_auction_results()->Add() =
        std::move(auction_result);
  }
  if (test_score_ads_request_config.enforce_kanon) {
    raw_request.set_enforce_kanon(*test_score_ads_request_config.enforce_kanon);
  }
  ScoreAdsRequest request;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request.set_key_id(kKeyId);
  return request;
}

ScoreAdsRequest BuildScoreAdsRequestForPAS(
    const TestScoreAdsRequestConfig& test_score_ads_request_config) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (int i = 0; i < test_score_ads_request_config.desired_ad_count; i++) {
    auto ad = GetTestAdWithBidMetadataForPAS(test_score_ads_request_config);
    raw_request.mutable_per_buyer_signals()->try_emplace(
        ad.owner(), test_score_ads_request_config.test_buyer_reporting_signals
                        .buyer_signals);
    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
    *raw_request.mutable_protected_app_signals_ad_bids()->Add() = ad;
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");
  if (test_score_ads_request_config.generate_scoring_signals) {
    raw_request.set_scoring_signals(trusted_scoring_signals);
  }
  raw_request.set_enable_debug_reporting(
      test_score_ads_request_config.enable_debug_reporting);
  raw_request.set_seller(test_score_ads_request_config.seller);
  // top_level_seller is expected to be set only for component auctions
  if (!test_score_ads_request_config.top_level_seller.empty()) {
    raw_request.set_top_level_seller(
        test_score_ads_request_config.top_level_seller);
  }
  raw_request.set_publisher_hostname(kPublisherHostname);
  raw_request.set_auction_signals(
      test_score_ads_request_config.test_buyer_reporting_signals
          .auction_signals);
  raw_request.mutable_consented_debug_config()->set_is_consented(
      test_score_ads_request_config.is_consented);
  raw_request.mutable_consented_debug_config()->set_token(kTestConsentToken);
  raw_request.set_seller_data_version(
      test_score_ads_request_config.seller_data_version);
  ScoreAdsRequest request;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request.set_key_id(kKeyId);
  return request;
}

void LoadAndRunScoreAdsForPA(
    const AuctionServiceRuntimeConfig& runtime_config,
    const TestScoreAdsRequestConfig& test_score_ads_request_config,
    absl::string_view buyer_udf, absl::string_view seller_udf,
    ScoreAdsResponse& response) {
  V8Dispatcher dispatcher;
  V8DispatchClient dispatch_client(dispatcher);
  ScoreAdsRequest request = BuildScoreAdsRequest(test_score_ads_request_config);
  LoadPABuyerAndSellerCode(request, dispatcher, runtime_config, buyer_udf,
                           seller_udf);
  RunTestScoreAds(dispatch_client, request, runtime_config, response);
}

void LoadAndRunScoreAdsForPAS(
    const AuctionServiceRuntimeConfig& runtime_config,
    const TestScoreAdsRequestConfig& test_score_ads_request_config,
    absl::string_view buyer_udf, absl::string_view seller_udf,
    ScoreAdsResponse& response) {
  V8Dispatcher dispatcher;
  V8DispatchClient dispatch_client(dispatcher);
  ScoreAdsRequest request =
      BuildScoreAdsRequestForPAS(test_score_ads_request_config);
  LoadPASBuyerAndSellerCode(request, dispatcher, runtime_config, buyer_udf,
                            seller_udf);
  RunTestScoreAds(dispatch_client, request, runtime_config, response);
}

}  // namespace privacy_sandbox::bidding_auction_servers
