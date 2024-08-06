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

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper_test_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper_test_constants.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ::testing::AnyNumber;

constexpr absl::string_view kKeyId = "key_id";
constexpr absl::string_view kSecret = "secret";
constexpr absl::string_view kTestConsentToken = "testConsentToken";
constexpr absl::string_view kEurosIsoCode = "EUR";
constexpr absl::string_view kPublisherHostname = "fenceStreetJournal.com";

AdWithBidMetadata GetTestAdWithBidMetadata(
    const TestScoreAdsRequestConfig& test_score_ads_request_config) {
  ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata ad;
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(MakeARandomString(), MakeARandomString(), 2));
  ad.set_bid(1.0);
  ad.add_ad_components("adComponent.com");
  if (test_score_ads_request_config.buyer_reporting_id.has_value()) {
    ad.set_buyer_reporting_id(
        *test_score_ads_request_config.buyer_reporting_id);
  }
  ad.set_bid_currency(kEurosIsoCode);
  ad.set_ad_cost(
      test_score_ads_request_config.test_buyer_reporting_signals.ad_cost);
  ad.set_modeling_signals(test_score_ads_request_config
                              .test_buyer_reporting_signals.modeling_signals);
  ad.set_join_count(
      test_score_ads_request_config.test_buyer_reporting_signals.join_count);
  ad.set_interest_group_name(
      test_score_ads_request_config.test_buyer_reporting_signals
          .interest_group_name);
  ad.set_interest_group_owner(
      test_score_ads_request_config.interest_group_owner);
  ad.set_render(absl::StrFormat(
      "%s/ads", test_score_ads_request_config.interest_group_owner));
  ad.set_recency(
      test_score_ads_request_config.test_buyer_reporting_signals.recency);
  return ad;
}

ScoreAdsRequest BuildScoreAdsRequest(
    const TestScoreAdsRequestConfig& test_score_ads_request_config,
    const std::vector<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>&
        ads) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (const auto& ad : ads) {
    raw_request.mutable_per_buyer_signals()->try_emplace(
        ad.interest_group_owner(),
        test_score_ads_request_config.test_buyer_reporting_signals
            .buyer_signals);
    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
    *raw_request.mutable_ad_bids()->Add() = ad;
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");
  raw_request.set_scoring_signals(trusted_scoring_signals);
  if (test_score_ads_request_config.enable_debug_reporting) {
    raw_request.set_enable_debug_reporting(
        test_score_ads_request_config.enable_debug_reporting);
  }
  raw_request.set_seller("http://seller.com");
  raw_request.set_publisher_hostname(kPublisherHostname);
  raw_request.set_auction_signals(
      test_score_ads_request_config.test_buyer_reporting_signals
          .auction_signals);
  raw_request.mutable_consented_debug_config()->set_is_consented(
      test_score_ads_request_config.is_consented);
  raw_request.mutable_consented_debug_config()->set_token(kTestConsentToken);
  if (!test_score_ads_request_config.top_level_seller.empty()) {
    raw_request.set_top_level_seller(
        test_score_ads_request_config.top_level_seller);
  }
  ScoreAdsRequest request;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request.set_key_id(kKeyId);
  return request;
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

void LoadTestSellerUdfWrapper(
    absl::string_view adtech_code_blob,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    V8Dispatcher& dispatcher) {
  std::string wrapper_js_blob = GetSellerWrappedCode(
      adtech_code_blob,
      auction_service_runtime_config.enable_report_result_url_generation);
  ASSERT_TRUE(dispatcher.LoadSync(kScoreAdBlobVersion, wrapper_js_blob).ok());
}

void LoadTestBuyerUdfWrapper(
    V8Dispatcher& dispatcher, const ScoreAdsRequest& request,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    AuctionType auction_type) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  ASSERT_TRUE(raw_request.ParseFromString(request.request_ciphertext()));
  if (!auction_service_runtime_config.enable_report_win_url_generation) {
    return;
  }
  for (const auto& ad_bid : raw_request.ad_bids()) {
    std::string wrapper_js_blob;
    if (auction_service_runtime_config.enable_report_win_input_noising) {
      wrapper_js_blob = GetBuyerWrappedCode(kBuyerBaseCodeWithValidation);
    } else {
      wrapper_js_blob = GetBuyerWrappedCode(kBuyerBaseCode);
    }
    absl::StatusOr<std::string> version =
        GetBuyerReportWinVersion(ad_bid.interest_group_owner(), auction_type);
    ASSERT_TRUE(version.ok());
    ASSERT_TRUE(dispatcher.LoadSync(*version, wrapper_js_blob).ok());
  }
}

void RunTestScoreAds(
    CodeDispatchClient& client, ScoreAdsRequest& request,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config,
    ScoreAdsResponse& response) {
  grpc::CallbackServerContext context;
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto score_ads_reactor_factory =
      [&client, async_reporter_local = std::move(async_reporter)](
          const ScoreAdsRequest* request, ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        return std::make_unique<ScoreAdsReactor>(
            client, request, response, std::make_unique<ScoreAdsNoOpLogger>(),
            key_fetcher_manager, crypto_client, async_reporter_local.get(),
            runtime_config);
      };

  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);

  service.ScoreAds(&context, &request, &response);
  std::this_thread::sleep_for(absl::ToChronoSeconds(absl::Seconds(2)));
}

// Loads Seller's udf containing scoreAd() and reportResult() as well as
// buyer's udf containing reportWin() for Protected Audience into Roma.
void LoadPABuyerAndSellerCode(
    const ScoreAdsRequest& request, V8Dispatcher& dispatcher,
    const AuctionServiceRuntimeConfig& runtime_config) {
  InitV8Dispatcher(dispatcher);
  LoadTestSellerUdfWrapper(kSellerBaseCode, runtime_config, dispatcher);
  LoadTestBuyerUdfWrapper(dispatcher, request, runtime_config,
                          AuctionType::kProtectedAudience);
}
}  // namespace

void LoadAndRunScoreAdsForPA(
    const AuctionServiceRuntimeConfig& runtime_config,
    const TestScoreAdsRequestConfig& test_score_ads_request_config,
    ScoreAdsResponse& response) {
  V8Dispatcher dispatcher;
  CodeDispatchClient dispatch_client(dispatcher);
  AdWithBidMetadata test_ad =
      GetTestAdWithBidMetadata(test_score_ads_request_config);
  ScoreAdsRequest request =
      BuildScoreAdsRequest(test_score_ads_request_config, {test_ad});
  LoadPABuyerAndSellerCode(request, dispatcher, runtime_config);
  RunTestScoreAds(dispatch_client, request, runtime_config, response);
}
}  // namespace privacy_sandbox::bidding_auction_servers
