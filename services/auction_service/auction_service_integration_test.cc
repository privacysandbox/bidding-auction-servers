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

#include "absl/strings/str_format.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/auction_service_integration_test_util.h"
#include "services/auction_service/auction_test_constants.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper_test_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper_test_constants.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/service_utils.h"
#include "services/common/test/utils/test_init.h"
#include "src/core/test/utils/proto_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using ::google::scp::core::test::EqualsProto;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ::testing::AnyNumber;
using ::testing::UnorderedPointwise;

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

constexpr absl::string_view js_code_with_loss_debug_url_exceeding_max_size =
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
                        direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      forDebuggingOnly.reportAdAuctionLoss("a".repeat(65538));
      forDebuggingOnly.reportAdAuctionWin("a".repeat(65536));
      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": true
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_urls_exceeding_max_total_size =
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
                        direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      forDebuggingOnly.reportAdAuctionLoss("a".repeat(1024));
      forDebuggingOnly.reportAdAuctionWin("a".repeat(1024));
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
                      direct_from_seller_signals) {
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
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
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
    const auto& scored_ad = raw_response.ad_score();
    // If no object was returned, the following two lines SHOULD fail.
    EXPECT_GT(scored_ad.desirability(), 0);
    EXPECT_FALSE(scored_ad.interest_group_name().empty());
    // If you see an error about a hash_map, it means invalid key, hence the
    // check on interest_group name above.
    EXPECT_EQ(
        scored_ad.render(),
        interest_group_to_ad.at(scored_ad.interest_group_name()).render());
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
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
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
        BuildScoreAdsRequest({.generate_scoring_signals = false});
    absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad =
        GetIGNameToAdMap(request);
    grpc::ClientContext client_context;
    grpc::Status status = stub->ScoreAds(&client_context, request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
        << server_common::ToAbslStatus(status);

    // This line may NOT break if the ad_score() object is empty.
    ScoreAdsResponse::ScoreAdsRawResponse raw_response;
    ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
    const auto& scored_ad = raw_response.ad_score();
    // If no object was returned, the following two lines SHOULD fail.
    EXPECT_GT(scored_ad.desirability(), 0);
    ASSERT_FALSE(scored_ad.interest_group_name().empty());
    // If you see an error about a hash_map, it means invalid key, hence the
    // check on interest_group name above.
    EXPECT_EQ(
        scored_ad.render(),
        interest_group_to_ad.at(scored_ad.interest_group_name()).render());
  }
}

void SellerCodeWrappingTestHelper(
    ScoreAdsRequest* request, ScoreAdsResponse* response,
    absl::string_view adtech_code_blob,
    const AuctionServiceRuntimeConfig& auction_service_runtime_config = {}) {
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
  std::string wrapper_js_blob = GetSellerWrappedCode(
      adtech_code_blob, /*enable_report_result_url_generation=*/false,
      /*enable_report_win_url_generation=*/false, buyer_origin_code_map);
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
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
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
       NoDebugReportsForSingleSellerAuctionWhenSamplingDisabled) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(raw_response.seller_debug_reports().reports_size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       HasDebugReportsForComponentAuctionWinnerWhenSamplingDisabled) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  DebugReports expected_debug_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: "https://example-ssp.com/debugWin"
          is_win_report: true
          is_seller_report: true
          is_component_win: true
        }
        reports {
          url: "https://example-ssp.com/debugLoss"
          is_win_report: false
          is_seller_report: true
          is_component_win: true
        }
      )pb",
      &expected_debug_reports));
  EXPECT_THAT(
      raw_response.seller_debug_reports().reports(),
      UnorderedPointwise(EqualsProto(), expected_debug_reports.reports()));
}

TEST_F(AuctionServiceIntegrationTest,
       HasDebugReportsForComponentAuctionWinnerFromGlobalThisMethodCall) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response,
                               js_code_with_global_this_debug_urls,
                               {.enable_seller_debug_url_generation = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  DebugReports expected_debug_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: "https://example-ssp.com/debugWin"
          is_win_report: true
          is_seller_report: true
          is_component_win: true
        }
        reports {
          url: "https://example-ssp.com/debugLoss"
          is_win_report: false
          is_seller_report: true
          is_component_win: true
        }
      )pb",
      &expected_debug_reports));
  EXPECT_THAT(
      raw_response.seller_debug_reports().reports(),
      UnorderedPointwise(EqualsProto(), expected_debug_reports.reports()));
}

TEST_F(AuctionServiceIntegrationTest, NoDebugReportsOnScriptCrash) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response,
                               js_code_throws_exception_with_debug_urls,
                               {.enable_seller_debug_url_generation = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  // If the script crashes, score is returned as 0 and hence no ad should win.
  EXPECT_EQ(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(raw_response.seller_debug_reports().reports_size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       HasMaxOneSampledDebugReportForSingleSellerAuction) {
  ScoreAdsRequest request = BuildScoreAdsRequest(
      {.enable_debug_reporting = true, .enable_sampled_debug_reporting = true});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true,
                                .debug_reporting_sampling_upper_bound = 1});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  DebugReports expected_debug_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: "https://example-ssp.com/debugWin"
          is_win_report: true
          is_seller_report: true
          is_component_win: false
        }
      )pb",
      &expected_debug_reports));
  EXPECT_THAT(raw_response.seller_debug_reports(),
              EqualsProto(expected_debug_reports));
}

TEST_F(AuctionServiceIntegrationTest,
       HasEmptyDebugReportWhenUrlFailsSamplingForSingleSellerAuction) {
  ScoreAdsRequest request = BuildScoreAdsRequest(
      {.enable_debug_reporting = true, .enable_sampled_debug_reporting = true});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true,
                                .debug_reporting_sampling_upper_bound = 0});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  DebugReports expected_debug_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: true
          is_seller_report: true
          is_component_win: false
        }
      )pb",
      &expected_debug_reports));
  EXPECT_THAT(raw_response.seller_debug_reports(),
              EqualsProto(expected_debug_reports));
}

TEST_F(AuctionServiceIntegrationTest,
       HasMaxOneSampledDebugReportOfEachTypeForComponentAuction) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .enable_sampled_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true,
                                .debug_reporting_sampling_upper_bound = 1});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  DebugReports expected_debug_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: "https://example-ssp.com/debugWin"
          is_win_report: true
          is_seller_report: true
          is_component_win: true
        }
        reports {
          url: "https://example-ssp.com/debugLoss"
          is_win_report: false
          is_seller_report: true
          is_component_win: true
        }
      )pb",
      &expected_debug_reports));
  EXPECT_THAT(
      raw_response.seller_debug_reports().reports(),
      UnorderedPointwise(EqualsProto(), expected_debug_reports.reports()));
}

TEST_F(AuctionServiceIntegrationTest,
       HasEmptyDebugReportsWhenUrlsFailSamplingForComponentAuction) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .enable_sampled_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true,
                                .debug_reporting_sampling_upper_bound = 0});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  DebugReports expected_debug_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: true
          is_seller_report: true
          is_component_win: true
        }
        reports {
          url: ""
          is_win_report: false
          is_seller_report: true
          is_component_win: true
        }
      )pb",
      &expected_debug_reports));
  EXPECT_THAT(
      raw_response.seller_debug_reports().reports(),
      UnorderedPointwise(EqualsProto(), expected_debug_reports.reports()));
}

TEST_F(AuctionServiceIntegrationTest,
       NoDebugReportsWhenDebugReportingDisabledOnServer) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = false});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(raw_response.seller_debug_reports().reports_size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       NoDebugReportsWhenDebugReportingDisabledInRequest) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = false,
                            .top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(raw_response.seller_debug_reports().reports_size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       NoSampledDebugReportsWhenSellerInCooldownLockout) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .enable_sampled_debug_reporting = true,
                            .in_cooldown_or_lockout = true});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               {.enable_seller_debug_url_generation = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(raw_response.seller_debug_reports().reports_size(), 0);
}

TEST_F(AuctionServiceIntegrationTest, DebugReportsRespectMaxUrlSize) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response,
                               js_code_with_loss_debug_url_exceeding_max_size,
                               {.enable_seller_debug_url_generation = true,
                                .max_allowed_size_debug_url_bytes = 65536});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  ASSERT_EQ(raw_response.seller_debug_reports().reports_size(), 1);
  auto debug_report = raw_response.seller_debug_reports().reports(0);
  EXPECT_EQ(debug_report.url().size(), 65536);
  EXPECT_TRUE(debug_report.is_win_report());
}

TEST_F(AuctionServiceIntegrationTest, DebugReportsRespectMaxTotalUrlsSize) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.enable_debug_reporting = true,
                            .top_level_seller = kTestTopLevelSeller});

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response,
                               js_code_with_debug_urls_exceeding_max_total_size,
                               {.enable_seller_debug_url_generation = true,
                                .max_allowed_size_all_debug_urls_kb = 1});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();

  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FALSE(scored_ad.has_debug_report_urls());
  ASSERT_EQ(raw_response.seller_debug_reports().reports_size(), 1);
  auto debug_report = raw_response.seller_debug_reports().reports(0);
  EXPECT_EQ(debug_report.url().size(), 1024);
  EXPECT_TRUE(debug_report.is_win_report());
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsSuccessfullyWithLogsWrapper) {
  ScoreAdsRequest request = BuildScoreAdsRequest();
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kSellerBaseCode,
                               {.enable_adtech_code_logging = true});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_GT(scored_ad.desirability(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoreAdsReturnsSuccessfullyWithLoggingDisabled) {
  ScoreAdsRequest request = BuildScoreAdsRequest();
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kSellerBaseCode,
                               {.enable_adtech_code_logging = false});
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_GT(scored_ad.desirability(), 0);
}

TEST_F(AuctionServiceIntegrationTest, CapturesRejectionReasonForRejectedAds) {
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
  SellerCodeWrappingTestHelper(&request, &response,
                               js_code_with_reject_reasons);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_GT(scored_ad.desirability(), 0);
  for (const auto& ad_rejection_reason : scored_ad.ad_rejection_reasons()) {
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
  SellerCodeWrappingTestHelper(&request, &response, kComponentAuctionCode);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), 1);
  EXPECT_EQ(scored_ad.bid(), 2);
  EXPECT_EQ(scored_ad.bid_currency(), "USD");
  EXPECT_FLOAT_EQ(scored_ad.incoming_bid_in_seller_currency(), 1.868);
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(),
            absl::StrCat("\"", kTestTopLevelSeller, "\""));
}

TEST_F(AuctionServiceIntegrationTest, FiltersUnallowedAdsForComponentAuction) {
  ScoreAdsRequest request =
      BuildScoreAdsRequest({.top_level_seller = kTestTopLevelSeller});
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response,
                               kSkipAdComponentAuctionCode);
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
  SellerCodeWrappingTestHelper(&request, &response, kTopLevelAuctionCode);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_GT(scored_ad.desirability(), 0);
  EXPECT_FLOAT_EQ(scored_ad.incoming_bid_in_seller_currency(), 1.868);

  // Should not be populated.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_TRUE(scored_ad.ad_metadata().empty());
  EXPECT_EQ(scored_ad.bid(), 0);
  EXPECT_TRUE(scored_ad.bid_currency().empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
