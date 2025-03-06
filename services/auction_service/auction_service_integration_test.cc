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

#include <thread>

#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper_test_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper_test_constants.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/service_utils.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ::testing::AnyNumber;

constexpr absl::string_view js_code = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));

      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_expects_null_scoring_signals = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));

      if (scoring_signals === null) {
        //Reshaped into AdScore
        return {
          "desirability": score,
          "allowComponentAuction": false
        };
      } else {
        throw new Error('Error: Scoring signals were not null!');
      }
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_urls = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      forDebuggingOnly.reportAdAuctionLoss("https://example-ssp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-ssp.com/debugWin");
      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": true
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_very_large_debug_urls = R"JS_CODE(
  function fibonacci(num) {
  if (num <= 1) return 1;
  return fibonacci(num - 1) + fibonacci(num - 2);
  }

  const generateRandomUrl = (length) => {
    let result = '';
    const characters =
      'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    const charactersLength = characters.length;
    const baseUrl = "https://example.com?randomParam=";
    const lengthOfRandomChars = length - baseUrl.length;
    for (let i = 0; i < lengthOfRandomChars; i++) {
      result += characters.charAt(Math.floor(Math.random() * charactersLength));
    }
    return baseUrl + result;
  };

  function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      forDebuggingOnly.reportAdAuctionLoss(generateRandomUrl(65538));
      forDebuggingOnly.reportAdAuctionWin(generateRandomUrl(65536));
      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": true
      };
  }
  )JS_CODE";

constexpr absl::string_view js_code_with_global_this_debug_urls = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      globalThis.forDebuggingOnly.reportAdAuctionLoss("https://example-ssp.com/debugLoss");
      globalThis.forDebuggingOnly.reportAdAuctionWin("https://example-ssp.com/debugWin");
      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": true
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception_with_debug_urls =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-ssp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-ssp.com/debugWin");
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_reject_reasons = R"JS_CODE(

    function reject_reason(ad_metadata) {
      if(ad_metadata && ad_metadata.rejectReason) {
        if(ad_metadata.rejectReason == 1) {
          return "invalid-bid";
        } else if(ad_metadata.rejectReason == 2) {
          return "bid-below-auction-floor";
        } else if(ad_metadata.rejectReason == 3) {
          return "pending-approval-by-exchange";
        } else if(ad_metadata.rejectReason == 4) {
          return "disapproved-by-exchange";
        } else if(ad_metadata.rejectReason == 5) {
          return "blocked-by-publisher";
        } else if(ad_metadata.rejectReason == 6) {
          return "language-exclusions";
        } else if(ad_metadata.rejectReason == 7) {
          return "category-exclusions";
        }
      }
      return "not-available";
    }

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      bid_metadata,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      var rejectReason = reject_reason(ad_metadata);
      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": false,
        "rejectReason": rejectReason,
      };
    }
  )JS_CODE";

struct TestBuyerReportingSignals {
  absl::string_view seller = kTestSeller;
  std::string interest_group_name = absl::StrCat(kTestInterestGroupName);
  double ad_cost = kTestAdCost;
  long recency = kTestRecency;
  int modeling_signals = kTestModelingSignals;
  int join_count = kTestJoinCount;
  std::string buyer_signals = absl::StrCat(kTestBuyerSignalsArr);
  std::string auction_signals = absl::StrCat(kTestAuctionSignalsArr);
  uint32_t data_version = kTestDataVersion;
};

// TODO: Unify this with the other one of the same name.
struct TestScoreAdsRequestConfig {
  const bool enable_debug_reporting = false;
  const int desired_ad_count = 90;
  absl::string_view top_level_seller = "";
  absl::string_view component_level_seller = "";
  const bool is_consented = false;
  std::optional<std::string> buyer_reporting_id = "";
  const uint32_t seller_data_version = kTestSellerDataVersion;
  absl::string_view seller = kTestSeller;
  const bool gen_scoring_signals = true;
};

ScoreAdsRequest::ScoreAdsRawRequest BuildScoreAdsRawRequest(
    const TestScoreAdsRequestConfig& config = TestScoreAdsRequestConfig{}) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (int i = 0; i < config.desired_ad_count; i++) {
    auto ad = MakeARandomAdWithBidMetadata(/*min_bid=*/1, /*max_bid=*/1);
    ad.set_bid_currency(kEurosIsoCode);
    ad.set_interest_group_owner(kDefaultBarInterestGroupOwner);
    ad.set_render(
        absl::StrFormat("%s/ads?id=%d", kDefaultBarInterestGroupOwner, i));
    *raw_request.mutable_ad_bids()->Add() = ad;
    raw_request.mutable_per_buyer_signals()->try_emplace(
        ad.interest_group_owner(), MakeARandomString());

    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");
  if (config.gen_scoring_signals) {
    raw_request.set_scoring_signals(trusted_scoring_signals);
  }
  raw_request.set_enable_debug_reporting(config.enable_debug_reporting);
  raw_request.set_seller(config.seller);
  raw_request.set_publisher_hostname(kPublisherHostname);
  raw_request.set_top_level_seller(config.top_level_seller);
  raw_request.set_seller_data_version(config.seller_data_version);
  return raw_request;
}

ScoreAdsRequest BuildScoreAdsRequest(
    const TestScoreAdsRequestConfig& config = TestScoreAdsRequestConfig{}) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request =
      BuildScoreAdsRawRequest(config);
  ScoreAdsRequest request;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request.set_key_id(kKeyId);
  return request;
}

ScoreAdsRequest BuildTopLevelAuctionScoreAdsRequest(
    const TestComponentAuctionResultData& component_auction_result_data,
    bool enable_debug_reporting = false, int desired_ad_count = 20,
    absl::string_view seller = kTestSeller) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  for (int i = 0; i < desired_ad_count; i++) {
    auto auction_result = MakeARandomComponentAuctionResultWithReportingUrls(
        component_auction_result_data);
    *raw_request.mutable_component_auction_results()->Add() = auction_result;
  }
  raw_request.set_enable_debug_reporting(enable_debug_reporting);
  raw_request.set_seller(seller);
  ScoreAdsRequest request;
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request.set_key_id(kKeyId);
  return request;
}

// Build a lookup map for AdWithBidMetadatas by IG Name.
absl::flat_hash_map<std::string, AdWithBidMetadata> GetIGNameToAdMap(
    const ScoreAdsRequest& request) {
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  raw_request.ParseFromString(request.request_ciphertext());
  for (const AdWithBidMetadata& ad_with_bid_metadata : raw_request.ad_bids()) {
    interest_group_to_ad.try_emplace(ad_with_bid_metadata.interest_group_name(),
                                     ad_with_bid_metadata);
  }
  return interest_group_to_ad;
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

class AuctionServiceIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<ScoreAdsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
  }
};

TEST_F(AuctionServiceIntegrationTest, ScoresAdsWithCustomScoringLogic) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  ASSERT_TRUE(dispatcher.Init().ok());
  ASSERT_TRUE(dispatcher
                  .LoadSync(kScoreAdBlobVersion,
                            GetSellerWrappedCode(
                                std::string(js_code),
                                kEnableReportResultUrlGenerationFalse,
                                kEnableReportResultWinGenerationFalse, {}))
                  .ok());
  std::unique_ptr<MockAsyncReporter> async_reporter_ =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto score_ads_reactor_factory =
      [&client, async_reporter_ = std::move(async_reporter_)](
          grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<ScoreAdsBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<ScoreAdsNoOpLogger>();
        }
        return std::make_unique<ScoreAdsReactor>(
            context, client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, *async_reporter_,
            runtime_config);
      };
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.default_score_ad_version = kScoreAdBlobVersion;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Auction>(result.port);

  int requests_to_test = 10;
  for (int i = 0; i < requests_to_test; i++) {
    ScoreAdsResponse response;
    ScoreAdsRequest request = BuildScoreAdsRequest();
    absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad =
        GetIGNameToAdMap(request);
    grpc::ClientContext client_context;
    grpc::Status status = stub->ScoreAds(&client_context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
        << server_common::ToAbslStatus(status);

    // This line may NOT break if the ad_score() object is empty.
    ScoreAdsResponse::ScoreAdsRawResponse raw_response;
    ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
    const auto& scoredAd = raw_response.ad_score();
    // If no object was returned, the following two lines SHOULD fail.
    EXPECT_GT(scoredAd.desirability(), 0);
    EXPECT_FALSE(scoredAd.interest_group_name().empty());
    // If you see an error about a hash_map, it means invalid key, hence the
    // check on interest_group name above.
    EXPECT_EQ(scoredAd.render(),
              interest_group_to_ad.at(scoredAd.interest_group_name()).render());
  }
}

TEST_F(AuctionServiceIntegrationTest, ScoresAdsDespiteNoScoringSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  ASSERT_TRUE(dispatcher.Init().ok());
  ASSERT_TRUE(
      dispatcher
          .LoadSync(kScoreAdBlobVersion,
                    GetSellerWrappedCode(
                        std::string(js_code_expects_null_scoring_signals),
                        kEnableReportResultUrlGenerationFalse,
                        kEnableReportResultWinGenerationFalse, {}))
          .ok());
  std::unique_ptr<MockAsyncReporter> async_reporter_ =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  auto score_ads_reactor_factory =
      [&client, async_reporter_ = std::move(async_reporter_)](
          grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<ScoreAdsBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<ScoreAdsNoOpLogger>();
        }
        return std::make_unique<ScoreAdsReactor>(
            context, client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, *async_reporter_,
            runtime_config);
      };
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.require_scoring_signals_for_scoring = false;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Auction>(result.port);

  int requests_to_test = 10;
  for (int i = 0; i < requests_to_test; i++) {
    ScoreAdsResponse response;
    ScoreAdsRequest request =
        BuildScoreAdsRequest({.gen_scoring_signals = false});
    absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad =
        GetIGNameToAdMap(request);
    grpc::ClientContext client_context;
    grpc::Status status = stub->ScoreAds(&client_context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
        << server_common::ToAbslStatus(status);

    // This line may NOT break if the ad_score() object is empty.
    ScoreAdsResponse::ScoreAdsRawResponse raw_response;
    ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
    const auto& scoredAd = raw_response.ad_score();
    // If no object was returned, the following two lines SHOULD fail.
    EXPECT_GT(scoredAd.desirability(), 0);
    ASSERT_FALSE(scoredAd.interest_group_name().empty());
    // If you see an error about a hash_map, it means invalid key, hence the
    // check on interest_group name above.
    EXPECT_EQ(scoredAd.render(),
              interest_group_to_ad.at(scoredAd.interest_group_name()).render());
  }
}

void SellerCodeWrappingTestHelper(
    ScoreAdsRequest* request, ScoreAdsResponse* response,
    absl::string_view adtech_code_blob, bool enable_seller_debug_url_generation,
    bool enable_debug_reporting, bool enable_adtech_code_logging = false,
    bool enable_report_result_url_generation = false,
    bool enable_report_win_url_generation = false,
    bool enable_protected_app_signals = false,
    bool enable_report_win_input_noising = false) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  ASSERT_TRUE(dispatcher.Init().ok());
  std::unique_ptr<MockAsyncReporter> async_reporter_ =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  absl::flat_hash_map<std::string, std::string> buyer_origin_code_map;
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  ASSERT_TRUE(raw_request.ParseFromString(request->request_ciphertext()));
  if (enable_report_win_url_generation) {
    for (const auto& ad_bid : raw_request.ad_bids()) {
      if (enable_report_win_input_noising) {
        buyer_origin_code_map.try_emplace(ad_bid.interest_group_owner(),
                                          kBuyerBaseCodeWithValidation);
      } else {
        buyer_origin_code_map.try_emplace(ad_bid.interest_group_owner(),
                                          kBuyerBaseCode);
      }
    }
  }
  std::string wrapper_js_blob = GetSellerWrappedCode(
      adtech_code_blob, enable_report_result_url_generation,
      enable_report_win_url_generation, buyer_origin_code_map);
  ASSERT_TRUE(
      dispatcher.LoadSync(kScoreAdBlobVersion, std::move(wrapper_js_blob))
          .ok());

  auto score_ads_reactor_factory =
      [&client, async_reporter_ = std::move(async_reporter_)](
          grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
          ScoreAdsResponse* response,
          server_common::KeyFetcherManagerInterface* key_fetcher_manager,
          CryptoClientWrapperInterface* crypto_client,
          const AuctionServiceRuntimeConfig& runtime_config) {
        // You can manually flip this flag to turn benchmarking logging on or
        // off
        bool enable_benchmarking = true;
        std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarking_logger;
        if (enable_benchmarking) {
          benchmarking_logger = std::make_unique<ScoreAdsBenchmarkingLogger>(
              FormatTime(absl::Now()));
        } else {
          benchmarking_logger = std::make_unique<ScoreAdsNoOpLogger>();
        }
        return std::make_unique<ScoreAdsReactor>(
            context, client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, *async_reporter_,
            runtime_config);
      };

  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config = {
      .enable_seller_debug_url_generation = enable_seller_debug_url_generation,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_win_url_generation,
      .enable_protected_app_signals = enable_protected_app_signals,
      .enable_report_win_input_noising = enable_report_win_input_noising,
      .default_score_ad_version = kScoreAdBlobVersion};
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Auction>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->ScoreAds(&client_context, *request, response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);
  // This line may NOT break if the ad_score() object is empty.
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsDoesNotReturnDebugUrlsForSingleSellerAuction) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();

  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
  EXPECT_GT(scoredAd.ig_owner_highest_scoring_other_bids_map().size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsDebugUrlsForWinningAdForComponentAuction) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();

  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_TRUE(scoredAd.has_debug_report_urls());
  EXPECT_EQ(scoredAd.debug_report_urls().auction_debug_win_url(),
            "https://example-ssp.com/debugWin");
  EXPECT_EQ(scoredAd.debug_report_urls().auction_debug_loss_url(),
            "https://example-ssp.com/debugLoss");
  EXPECT_GT(scoredAd.ig_owner_highest_scoring_other_bids_map().size(), 0);
}

TEST_F(AuctionServiceIntegrationTest, ScoreAdsDoesNotReturnLargeDebugUrls) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting,
                            .desired_ad_count = 1,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, js_code_with_very_large_debug_urls,
      enable_seller_debug_url_generation, enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();

  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_TRUE(scoredAd.has_debug_report_urls());
  EXPECT_GT(scoredAd.debug_report_urls().auction_debug_win_url().length(), 0);
  EXPECT_EQ(scoredAd.debug_report_urls().auction_debug_loss_url().length(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsDebugUrlsFromGlobalThisMethodCalls) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, js_code_with_global_this_debug_urls,
      enable_seller_debug_url_generation, enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();

  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_TRUE(scoredAd.has_debug_report_urls());
  EXPECT_EQ(scoredAd.debug_report_urls().auction_debug_win_url(),
            "https://example-ssp.com/debugWin");
  EXPECT_EQ(scoredAd.debug_report_urls().auction_debug_loss_url(),
            "https://example-ssp.com/debugLoss");
  EXPECT_GT(scoredAd.ig_owner_highest_scoring_other_bids_map().size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsDoesNotReturnDebugUrlsWhenDebugReportingDisabled) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = false;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting,
                            .top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
  EXPECT_GT(scoredAd.ig_owner_highest_scoring_other_bids_map().size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsDebugUrlsWithScriptCrashWhenAvailable) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting,
                            .top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, js_code_throws_exception_with_debug_urls,
      enable_seller_debug_url_generation, enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_EQ(scoredAd.desirability(), 0);
  // if the script crashes, score is returned as 0 and hence no ad should win.
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsDoesNotReturnDebugUrlsWithScriptCrash) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = enable_debug_reporting,
                            .top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_throws_exception,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_EQ(scoredAd.desirability(), 0);
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsSuccessFullyWithLogsWrapper) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  ScoreAdsRequest request = BuildScoreAdsRequest();
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsSuccessFullyWithLoggingDisabled) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = false;
  ScoreAdsRequest request = BuildScoreAdsRequest();
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
}

TEST_F(AuctionServiceIntegrationTest, CapturesRejectionReasonForRejectedAds) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, SellerRejectionReason>
      interest_group_to_rejection_reason;
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  int desired_ad_count = 90;
  absl::BitGen bitgen;
  for (int i = 0; i < desired_ad_count; i++) {
    int rejection_reason_index = absl::Uniform(bitgen, 0, 7);
    auto ad = MakeARandomAdWithBidMetadataWithRejectionReason(
        1, 10, 5, rejection_reason_index);
    *raw_request.mutable_ad_bids()->Add() = ad;
    interest_group_to_rejection_reason.try_emplace(
        ad.interest_group_name(),
        static_cast<SellerRejectionReason>(rejection_reason_index));

    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");
  raw_request.set_scoring_signals(trusted_scoring_signals);
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();
  request.set_key_id(kKeyId);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_reject_reasons,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  for (const auto& ad_rejection_reason : scoredAd.ad_rejection_reasons()) {
    // Validating that if seller rejection reason is not available, its not sent
    // in response.
    EXPECT_NE(ad_rejection_reason.rejection_reason(),
              SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
    const std::string& interest_group_name =
        ad_rejection_reason.interest_group_name();
    EXPECT_EQ(ad_rejection_reason.rejection_reason(),
              interest_group_to_rejection_reason.at(interest_group_name));
  }
}

TEST_F(AuctionServiceIntegrationTest, PopulatesOutputForComponentAuction) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kComponentAuctionCode,
                               /*enable_seller_debug_url_generation=*/false,
                               /*enable_debug_reporting=*/false);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_EQ(scoredAd.desirability(), 1);
  EXPECT_EQ(scoredAd.bid(), 2);
  EXPECT_EQ(scoredAd.bid_currency(), "USD");
  EXPECT_FLOAT_EQ(scoredAd.incoming_bid_in_seller_currency(), 1.868);
  EXPECT_TRUE(scoredAd.allow_component_auction());
  EXPECT_EQ(scoredAd.ad_metadata(),
            absl::StrCat("\"", kTestTopLevelSeller, "\""));
}

TEST_F(AuctionServiceIntegrationTest, FiltersUnallowedAdsForComponentAuction) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kSkipAdComponentAuctionCode,
                               /*enable_seller_debug_url_generation=*/false,
                               /*enable_debug_reporting=*/false);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(AuctionServiceIntegrationTest,
       ProcessTopLevelAuctionInputAndReturnWinner) {
  TestComponentAuctionResultData component_auction_result_data = {
      .test_component_seller = kTestComponentSeller,
      .generation_id = kTestGenerationId,
      .test_ig_owner = kTestComponentIgOwner,
      .test_component_win_reporting_url = kTestComponentWinReportingUrl,
      .test_component_report_result_url = kTestComponentWinReportingUrl,
      .test_component_event = kTestComponentEvent,
      .test_component_interaction_reporting_url =
          kTestComponentInteractionReportingUrl};
  ScoreAdsRequest request =
      BuildTopLevelAuctionScoreAdsRequest(component_auction_result_data);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kTopLevelAuctionCode,
                               /*enable_seller_debug_url_generation=*/false,
                               /*enable_debug_reporting=*/false);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_FLOAT_EQ(scoredAd.incoming_bid_in_seller_currency(), 1.868);

  // Should not be populated.
  EXPECT_FALSE(scoredAd.allow_component_auction());
  EXPECT_TRUE(scoredAd.ad_metadata().empty());
  EXPECT_EQ(scoredAd.bid(), 0);
  EXPECT_TRUE(scoredAd.bid_currency().empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
