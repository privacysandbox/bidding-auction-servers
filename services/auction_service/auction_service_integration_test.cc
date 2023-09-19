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
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_service.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper_test_constants.h"
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

constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr bool kEnableReportResultUrlGenerationFalse = false;
constexpr bool kEnableReportResultWinGenerationFalse = false;
constexpr char kTestSeller[] = "http://seller.com";

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
                      device_signals,
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

constexpr absl::string_view js_code_with_debug_urls = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd( ad_metadata,
                      bid,
                      auction_config,
                      scoring_signals,
                      device_signals,
                      direct_from_seller_signals
    ) {
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      forDebuggingOnly.reportAdAuctionLoss("https://example-ssp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-ssp.com/debugWin");
      //Reshaped into AdScore
      return {
        "desirability": score,
        "allowComponentAuction": false
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
                      device_signals,
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
                      device_signals,
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
                      device_signals,
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

void BuildScoreAdsRequest(
    ScoreAdsRequest* request,
    absl::flat_hash_map<std::string, AdWithBidMetadata>* interest_group_to_ad,
    bool enable_debug_reporting = false, int desired_ad_count = 90,
    const std::string& seller = kTestSeller) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (int i = 0; i < desired_ad_count; i++) {
    auto ad = MakeARandomAdWithBidMetadata(1, 10);
    *raw_request.mutable_ad_bids()->Add() = ad;
    raw_request.mutable_per_buyer_signals()->try_emplace(
        ad.interest_group_owner(), MakeARandomString());
    interest_group_to_ad->try_emplace(ad.interest_group_name(), ad);

    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");
  raw_request.set_scoring_signals(trusted_scoring_signals);
  if (enable_debug_reporting) {
    raw_request.set_enable_debug_reporting(enable_debug_reporting);
  }
  raw_request.set_seller(seller);
  *request->mutable_request_ciphertext() = raw_request.SerializeAsString();
  request->set_key_id(kKeyId);
}

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.

  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
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
          [](const std::string& plaintext_payload, const std::string& secret) {
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
    server_common::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::TelemetryConfig::PROD);
    metric::AuctionContextMap(
        server_common::BuildDependentConfig(config_proto));
  }
};

TEST_F(AuctionServiceIntegrationTest, ScoresAdsWithCustomScoringLogic) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  DispatchConfig config;
  ASSERT_TRUE(dispatcher.Init(config).ok());
  ASSERT_TRUE(dispatcher
                  .LoadSync(1, GetSellerWrappedCode(
                                   std::string(js_code),
                                   kEnableReportResultUrlGenerationFalse,
                                   kEnableReportResultWinGenerationFalse, {}))
                  .ok());
  auto score_ads_reactor_factory =
      [&client](const ScoreAdsRequest* request, ScoreAdsResponse* response,
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
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, std::move(async_reporter),
            runtime_config);
      };
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.encryption_enabled = true;
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);

  int requests_to_test = 10;
  for (int i = 0; i < requests_to_test; i++) {
    ScoreAdsRequest request;
    ScoreAdsResponse response;
    absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
    BuildScoreAdsRequest(&request, &interest_group_to_ad);
    service.ScoreAds(&context, &request, &response);

    std::this_thread::sleep_for(absl::ToChronoSeconds(absl::Seconds(2)));

    // This line may NOT break if the ad_score() object is empty.
    ScoreAdsResponse::ScoreAdsRawResponse raw_response;
    raw_response.ParseFromString(response.response_ciphertext());
    const auto& scoredAd = raw_response.ad_score();
    // If no object was returned, the following two lines SHOULD fail.
    EXPECT_GT(scoredAd.desirability(), 0);
    EXPECT_FALSE(scoredAd.interest_group_name().empty());
    // If you see an error about a hash_map, it means invalid key, hence the
    // check on interest_group name above.
    EXPECT_EQ(scoredAd.render(),
              interest_group_to_ad.at(scoredAd.interest_group_name()).render());
  }
  EXPECT_TRUE(dispatcher.Stop().ok());
}

void SellerCodeWrappingTestHelper(
    ScoreAdsRequest* request, ScoreAdsResponse* response,
    absl::string_view adtech_code_blob, bool enable_seller_debug_url_generation,
    bool enable_debug_reporting, bool enable_adtech_code_logging = false,
    bool enable_report_result_url_generation = false,
    bool enable_report_win_url_generation = false) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  CodeDispatchClient client(dispatcher);
  DispatchConfig config;
  ASSERT_TRUE(dispatcher.Init(config).ok());
  absl::flat_hash_map<std::string, std::string> buyer_origin_code_map;
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  raw_request.ParseFromString(request->request_ciphertext());
  if (enable_report_win_url_generation) {
    for (auto ad_bid : raw_request.ad_bids()) {
      buyer_origin_code_map.try_emplace(ad_bid.interest_group_owner(),
                                        kBuyerBaseCode);
    }
  }
  std::string wrapper_js_blob = GetSellerWrappedCode(
      adtech_code_blob, enable_report_result_url_generation,
      enable_report_win_url_generation, buyer_origin_code_map);
  ASSERT_TRUE(dispatcher.LoadSync(1, wrapper_js_blob).ok());

  auto score_ads_reactor_factory =
      [&client](const ScoreAdsRequest* request, ScoreAdsResponse* response,
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
        std::unique_ptr<MockAsyncReporter> async_reporter =
            std::make_unique<MockAsyncReporter>(
                std::make_unique<MockHttpFetcherAsync>());
        return std::make_unique<ScoreAdsReactor>(
            client, request, response, std::move(benchmarking_logger),
            key_fetcher_manager, crypto_client, std::move(async_reporter),
            runtime_config);
      };

  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
  config_client.SetFlagForTest(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config = {
      .encryption_enabled = true,
      .enable_seller_debug_url_generation = enable_seller_debug_url_generation,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_win_url_generation};
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);

  service.ScoreAds(&context, request, response);
  std::this_thread::sleep_for(absl::ToChronoSeconds(absl::Seconds(2)));
  // This line may NOT break if the ad_score() object is empty.
  EXPECT_TRUE(dispatcher.Stop().ok());
}

TEST_F(AuctionServiceIntegrationTest, ScoresAdsReturnsDebugUrlsForWinningAd) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);

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

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsWillNotReturnDebugUrlsForWinningAd) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = false;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_with_debug_urls,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
  EXPECT_GT(scoredAd.ig_owner_highest_scoring_other_bids_map().size(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsWillReturnDebugUrlsWithScriptCrash) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, js_code_throws_exception_with_debug_urls,
      enable_seller_debug_url_generation, enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_EQ(scoredAd.desirability(), 0);
  // if the script crashes, score is returned as 0 and hence no ad should win.
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsWillNotReturnDebugUrlsWithScriptCrash) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, js_code_throws_exception,
                               enable_seller_debug_url_generation,
                               enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_EQ(scoredAd.desirability(), 0);
  EXPECT_FALSE(scoredAd.has_debug_report_urls());
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessFullyWithLogsWrapper) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessFullyWithLoggingDisabled) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = false;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessFullyWithReportingEnabled) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(
      scoredAd.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

TEST_F(AuctionServiceIntegrationTest,
       ReportingUrlsForBuyerEmptyWhenSellerInputIsMissing) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  int desired_ad_count = 90;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting,
                       desired_ad_count, "");
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_TRUE(scoredAd.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(scoredAd.win_reporting_urls()
                  .buyer_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessFullyWithReportWinDisabled) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = false;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_TRUE(scoredAd.win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(scoredAd.win_reporting_urls()
                  .buyer_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
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
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  for (const auto& ad_rejection_reason : scoredAd.ad_rejection_reasons()) {
    // Validating that if seller rejection reason is not available, its not sent
    // in response.
    EXPECT_NE(ad_rejection_reason.rejection_reason(),
              SellerRejectionReason::SELLER_REJECTION_REASON_NOT_AVAILABLE);
    std::string interest_group_name = ad_rejection_reason.interest_group_name();
    EXPECT_EQ(ad_rejection_reason.rejection_reason(),
              interest_group_to_rejection_reason.at(interest_group_name));
  }
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
