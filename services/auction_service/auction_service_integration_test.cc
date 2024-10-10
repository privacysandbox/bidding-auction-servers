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
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr bool kEnableReportResultUrlGenerationFalse = false;
constexpr bool kEnableReportResultWinGenerationFalse = false;
constexpr absl::string_view kTestSeller = "http://seller.com";
constexpr char kTestTopLevelSeller[] = "topLevelSeller";
constexpr char kTestInterestGroupName[] = "testInterestGroupName";
constexpr double kTestAdCost = 2.0;
constexpr long kTestRecency = 3;
constexpr int kTestModelingSignals = 4;
constexpr int kTestJoinCount = 5;
constexpr char kTestBuyerSignals[] = R"([1,"test",[2]])";
constexpr char kTestAuctionSignals[] = R"([[3,"test",[4]]])";
constexpr char kTestConsentToken[] = "testConsentToken";
constexpr char kEurosIsoCode[] = "EUR";
constexpr char kPublisherHostname[] = "fenceStreetJournal.com";
constexpr char kDefaultBarInterestGroupOwner[] = "barStandardAds.com";
constexpr char kTestReportWinUrlWithSignals[] =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=testInterestGroupName&adCost=2&"
    "modelingSignals=4&recency=3&madeHighestScoringOtherBid=true&joinCount=5&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&"
    "auctionSignals=3,test,4&desirability=undefined";

constexpr char kTestReportWinUrlWithBuyerReportingId[] =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=undefined&adCost=2&"
    "modelingSignals=4&recency=3&madeHighestScoringOtherBid=true&joinCount=5&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&"
    "auctionSignals=3,test,4&desirability=undefined&buyerReportingId="
    "testBuyerReportingId";

constexpr char kTestReportWinUrlWithNoising[] =
    "http://test.com?seller=http://"
    "seller.com&interestGroupName=testInterestGroupName&adCost=2&"
    "madeHighestScoringOtherBid=true&"
    "signalsForWinner={\"testSignal\":\"testValue\"}&perBuyerSignals=1,test,2&"
    "auctionSignals=3,test,4&desirability=undefined";

constexpr char kTestComponentWinReportingUrl[] =
    "http://componentReportingUrl.com";
constexpr char kTestComponentEvent[] = "click";
constexpr char kTestComponentInteractionReportingUrl[] =
    "http://componentInteraction.com";
constexpr absl::string_view kTestComponentSeller = "http://componentSeller.com";
constexpr char kTestGenerationId[] = "testGenerationId";
constexpr char kTestComponentIgOwner[] = "http://componentIgOwner.com";

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
        "allowComponentAuction": false
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
        "allowComponentAuction": false
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
  std::string interest_group_name = kTestInterestGroupName;
  double ad_cost = kTestAdCost;
  long recency = kTestRecency;
  int modeling_signals = kTestModelingSignals;
  int join_count = kTestJoinCount;
  std::string buyer_signals = kTestBuyerSignals;
  std::string auction_signals = kTestAuctionSignals;
};

struct TestScoreAdsRequestConfig {
  const TestBuyerReportingSignals& test_buyer_reporting_signals;
  bool enable_debug_reporting = false;
  int desired_ad_count = 90;
  absl::string_view top_level_seller = "";
  absl::string_view component_level_seller = "";
  bool is_consented = false;
  std::optional<std::string> buyer_reporting_id;
};

void BuildScoreAdsRequest(
    ScoreAdsRequest* request,
    absl::flat_hash_map<std::string, AdWithBidMetadata>* interest_group_to_ad,
    bool enable_debug_reporting = false, int desired_ad_count = 90,
    absl::string_view seller = kTestSeller, bool component_auction = false) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (int i = 0; i < desired_ad_count; i++) {
    auto ad = MakeARandomAdWithBidMetadata(/*min_bid=*/1, /*max_bid=*/1);
    ad.set_bid_currency(kEurosIsoCode);
    ad.set_interest_group_owner(kDefaultBarInterestGroupOwner);
    ad.set_render(
        absl::StrFormat("%s/ads?id=%d", kDefaultBarInterestGroupOwner, i));
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
  raw_request.set_publisher_hostname(kPublisherHostname);
  if (component_auction) {
    raw_request.set_top_level_seller(kTestTopLevelSeller);
  }
  *request->mutable_request_ciphertext() = raw_request.SerializeAsString();
  request->set_key_id(kKeyId);
}

void BuildTopLevelAuctionScoreAdsRequest(
    const TestComponentAuctionResultData& component_auction_result_data,
    ScoreAdsRequest* request, bool enable_debug_reporting = false,
    int desired_ad_count = 20, absl::string_view seller = kTestSeller) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string generation_id = MakeARandomString();
  for (int i = 0; i < desired_ad_count; i++) {
    auto auction_result = MakeARandomComponentAuctionResultWithReportingUrls(
        component_auction_result_data);
    *raw_request.mutable_component_auction_results()->Add() = auction_result;
  }
  if (enable_debug_reporting) {
    raw_request.set_enable_debug_reporting(enable_debug_reporting);
  }
  raw_request.set_seller(seller);
  *request->mutable_request_ciphertext() = raw_request.SerializeAsString();
  request->set_key_id(kKeyId);
}

void BuildComponentScoreAdsRequest(
    ScoreAdsRequest* request,
    absl::flat_hash_map<std::string, AdWithBidMetadata>* interest_group_to_ad,
    bool enable_debug_reporting = false, int desired_ad_count = 90,
    absl::string_view seller = kTestSeller) {
  BuildScoreAdsRequest(request, interest_group_to_ad, enable_debug_reporting,
                       desired_ad_count, seller, /*component_auction=*/true);
}

void BuildScoreAdsRequestForReporting(
    ScoreAdsRequest* request,
    absl::flat_hash_map<std::string, AdWithBidMetadata>* interest_group_to_ad,
    const TestScoreAdsRequestConfig& test_score_ads_request_config) {
  ScoreAdsRequest::ScoreAdsRawRequest raw_request;
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  for (int i = 0; i < test_score_ads_request_config.desired_ad_count; i++) {
    auto ad = MakeARandomAdWithBidMetadata(/*min_bid=*/1, /*max_bid=*/1);
    if (test_score_ads_request_config.buyer_reporting_id.has_value()) {
      ad.set_buyer_reporting_id(
          test_score_ads_request_config.buyer_reporting_id.value());
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
    ad.set_interest_group_owner(kDefaultBarInterestGroupOwner);
    ad.set_render(
        absl::StrFormat("%s/ads?id=%d", kDefaultBarInterestGroupOwner, i));
    ad.set_recency(
        test_score_ads_request_config.test_buyer_reporting_signals.recency);
    *raw_request.mutable_ad_bids()->Add() = ad;
    raw_request.mutable_per_buyer_signals()->try_emplace(
        ad.interest_group_owner(),
        test_score_ads_request_config.test_buyer_reporting_signals
            .buyer_signals);
    interest_group_to_ad->try_emplace(ad.interest_group_name(), ad);

    std::string ad_signal = absl::StrFormat(
        "\"%s\":%s", ad.render(), R"JSON(["short", "test", "signal"])JSON");
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
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
  *request->mutable_request_ciphertext() = raw_request.SerializeAsString();
  request->set_key_id(kKeyId);
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
            key_fetcher_manager, crypto_client, async_reporter_.get(),
            runtime_config);
      };
  auto crypto_client = std::make_unique<MockCryptoClientWrapper>();
  SetupMockCryptoClientWrapper(*crypto_client);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /* public_key_fetcher= */ nullptr);
  AuctionServiceRuntimeConfig auction_service_runtime_config;
  auction_service_runtime_config.default_code_version = kScoreAdBlobVersion;
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
  raw_request.ParseFromString(request->request_ciphertext());
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
            key_fetcher_manager, crypto_client, async_reporter_.get(),
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
      .default_code_version = kScoreAdBlobVersion};
  AuctionService service(
      std::move(score_ads_reactor_factory), std::move(key_fetcher_manager),
      std::move(crypto_client), auction_service_runtime_config);

  service.ScoreAds(&context, request, response);
  std::this_thread::sleep_for(absl::ToChronoSeconds(absl::Seconds(2)));
  // This line may NOT break if the ad_score() object is empty.
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

TEST_F(AuctionServiceIntegrationTest, ScoresAdsDoesNotReturnsLargeDebugUrls) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting,
                       1);

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, js_code_with_very_large_debug_urls,
      enable_seller_debug_url_generation, enable_debug_reporting);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();

  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_TRUE(scoredAd.has_debug_report_urls());
  EXPECT_GT(scoredAd.debug_report_urls().auction_debug_win_url().length(), 0);
  EXPECT_EQ(scoredAd.debug_report_urls().auction_debug_loss_url().length(), 0);
}

TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsDebugUrlsForWinningAdFromGlobalThisMethodCalls) {
  bool enable_seller_debug_url_generation = true;
  bool enable_debug_reporting = true;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildScoreAdsRequest(&request, &interest_group_to_ad, enable_debug_reporting);

  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, js_code_with_global_this_debug_urls,
      enable_seller_debug_url_generation, enable_debug_reporting);
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

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessFullyWithReportingEnabled) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  bool enable_report_win_input_noising = false;
  TestBuyerReportingSignals test_buyer_reporting_signals;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = ""};
  BuildScoreAdsRequestForReporting(&request, &interest_group_to_ad,
                                   test_score_ads_request_config);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation,
      enable_report_win_input_noising);
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
      kTestReportWinUrlWithSignals);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest,
       AdScoreIncludesWinReportingUrlsAndBuyerReportingId) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  bool enable_report_win_input_noising = false;
  TestBuyerReportingSignals test_buyer_reporting_signals;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .buyer_reporting_id = kTestBuyerReportingId};
  BuildScoreAdsRequestForReporting(&request, &interest_group_to_ad,
                                   test_score_ads_request_config);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation,
      enable_report_win_input_noising);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_EQ(scoredAd.buyer_reporting_id(), kTestBuyerReportingId);
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
      kTestReportWinUrlWithBuyerReportingId);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest, ReportingSuccessfulWithNoising) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  bool enable_report_win_input_noising = true;
  bool enable_protected_app_signals = false;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = {}};
  BuildScoreAdsRequestForReporting(&request, &interest_group_to_ad,
                                   test_score_ads_request_config);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation,
      enable_protected_app_signals, enable_report_win_input_noising);
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
      kTestReportWinUrlWithNoising);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest,
       SuccessfulReportingWithProtectedAppSignalsEnabled) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  bool enable_protected_app_signals = true;

  TestBuyerReportingSignals test_buyer_reporting_signals;
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals};
  BuildScoreAdsRequestForReporting(&request, &interest_group_to_ad,
                                   test_score_ads_request_config);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kSellerBaseCode, enable_seller_debug_url_generation,
      enable_debug_reporting, enable_adtech_code_logging,
      enable_report_result_url_generation, enable_report_win_url_generation,
      enable_protected_app_signals);
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
      kTestReportWinUrlWithSignals);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessfullyWithReportingForComponentAuctions) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  TestBuyerReportingSignals test_buyer_reporting_signals;

  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .top_level_seller = kTestTopLevelSeller};
  BuildScoreAdsRequestForReporting(&request, &interest_group_to_ad,
                                   test_score_ads_request_config);
  ScoreAdsResponse response;
  ScoreAdsRequest::ScoreAdsRawRequest score_ads_raw_request;
  score_ads_raw_request.ParseFromString(request.request_ciphertext());
  ASSERT_FALSE(score_ads_raw_request.top_level_seller().empty());
  SellerCodeWrappingTestHelper(
      &request, &response, kComponentAuctionCode,
      enable_seller_debug_url_generation, enable_debug_reporting,
      enable_adtech_code_logging, enable_report_result_url_generation,
      enable_report_win_url_generation);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  // Make sure that this the AdScore we expect before checking its reporting
  // urls.
  EXPECT_FLOAT_EQ(scoredAd.desirability(), 1);
  EXPECT_FLOAT_EQ(scoredAd.buyer_bid(), 1);
  EXPECT_EQ(scoredAd.buyer_bid_currency(), "EUR");
  EXPECT_FLOAT_EQ(scoredAd.bid(), 2);
  EXPECT_FLOAT_EQ(scoredAd.incoming_bid_in_seller_currency(), 1.868);
  EXPECT_EQ(scoredAd.bid_currency(), "USD");
  EXPECT_EQ(scoredAd.allow_component_auction(), true);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentReportResultUrlWithEverything);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(
      scoredAd.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrlWithSignals);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest,
       ScoresAdsReturnsSuccessfullyWithReportingForTopLevelAuctions) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  TestBuyerReportingSignals test_buyer_reporting_signals;

  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestComponentAuctionResultData component_auction_result_data = {
      .test_component_seller = kTestComponentSeller,
      .generation_id = kTestGenerationId,
      .test_ig_owner = kTestComponentIgOwner,
      .test_component_win_reporting_url = kTestComponentWinReportingUrl,
      .test_component_report_result_url = kTestComponentWinReportingUrl,
      .test_component_event = kTestComponentEvent,
      .test_component_interaction_reporting_url =
          kTestComponentInteractionReportingUrl};
  BuildTopLevelAuctionScoreAdsRequest(component_auction_result_data, &request);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kTopLevelSellerCode,
      enable_seller_debug_url_generation, enable_debug_reporting,
      enable_adtech_code_logging, enable_report_result_url_generation,
      enable_report_win_url_generation);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestTopLevelReportResultUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentWinReportingUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestComponentInteractionReportingUrl);

  EXPECT_EQ(
      scoredAd.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestComponentWinReportingUrl);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestComponentEvent),
            kTestComponentInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
TEST_F(AuctionServiceIntegrationTest,
       ModifiedBidSetToBuyerBidInComponentAuctionsReportUrlIfNotPresent) {
  bool enable_seller_debug_url_generation = false;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  TestBuyerReportingSignals test_buyer_reporting_signals;

  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  TestScoreAdsRequestConfig test_score_ads_request_config = {
      .test_buyer_reporting_signals = test_buyer_reporting_signals,
      .top_level_seller = kTestTopLevelSeller};
  BuildScoreAdsRequestForReporting(&request, &interest_group_to_ad,
                                   test_score_ads_request_config);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(
      &request, &response, kComponentAuctionCodeWithNoModifiedBid,
      enable_seller_debug_url_generation, enable_debug_reporting,
      enable_adtech_code_logging, enable_report_result_url_generation,
      enable_report_win_url_generation);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scoredAd = raw_response.ad_score();
  EXPECT_GT(scoredAd.desirability(), 0);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestComponentReportResultUrlWithNoModifiedBid);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
  EXPECT_EQ(
      scoredAd.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrlWithSignals);
  EXPECT_EQ(scoredAd.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionReportingUrl);
}

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
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

// [[deprecated("DEPRECATED. Please use the tests in
// auction_service_reporting_integration_test instead")]]
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
    const std::string& interest_group_name =
        ad_rejection_reason.interest_group_name();
    EXPECT_EQ(ad_rejection_reason.rejection_reason(),
              interest_group_to_rejection_reason.at(interest_group_name));
  }
}

TEST_F(AuctionServiceIntegrationTest, PopulatesOutputForComponentAuction) {
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildComponentScoreAdsRequest(&request, &interest_group_to_ad);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kComponentAuctionCode,
                               /*enable_seller_debug_url_generation=*/false,
                               /*enable_debug_reporting=*/false);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
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
  ScoreAdsRequest request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> interest_group_to_ad;
  BuildComponentScoreAdsRequest(&request, &interest_group_to_ad);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kSkipAdComponentAuctionCode,
                               /*enable_seller_debug_url_generation=*/false,
                               /*enable_debug_reporting=*/false);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(AuctionServiceIntegrationTest,
       ProcessTopLevelAuctionInputAndReturnWinner) {
  ScoreAdsRequest request;
  TestComponentAuctionResultData component_auction_result_data = {
      .test_component_seller = kTestComponentSeller,
      .generation_id = kTestGenerationId,
      .test_ig_owner = kTestComponentIgOwner,
      .test_component_win_reporting_url = kTestComponentWinReportingUrl,
      .test_component_report_result_url = kTestComponentWinReportingUrl,
      .test_component_event = kTestComponentEvent,
      .test_component_interaction_reporting_url =
          kTestComponentInteractionReportingUrl};
  BuildTopLevelAuctionScoreAdsRequest(component_auction_result_data, &request);
  ScoreAdsResponse response;
  SellerCodeWrappingTestHelper(&request, &response, kTopLevelAuctionCode,
                               /*enable_seller_debug_url_generation=*/false,
                               /*enable_debug_reporting=*/false);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
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
