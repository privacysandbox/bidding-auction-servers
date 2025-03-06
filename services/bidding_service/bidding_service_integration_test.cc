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

#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/bidding_service.h"
#include "services/bidding_service/bidding_service_factories.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper_test_constants.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
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
using ::testing::AnyNumber;

// Must be ample time for generateBid() to complete, otherwise we risk
// flakiness. In production, generateBid() should run in no more than a few
// hundred milliseconds.
constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr char kAdRenderUrlPrefixForTest[] = "https://advertising.net/ad?";
constexpr char kTopLevelSeller[] = "top_level_seller";
constexpr char kTestConsentToken[] = "testConsentToken";
constexpr int kTestMultiBidLimit = 10;

constexpr uint32_t kDataVersion = 1991;

// While Roma demands JSON input and enforces it strictly, we follow the
// javascript style guide for returning objects here, so object keys are
// unquoted on output even though they MUST be quoted on input.
constexpr absl::string_view js_code_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        bidCurrency: "USD",
        adComponents: [
          "adComponent.com/comp?id=1",
          "adComponent.com/comp?id=2"
        ],
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_reporting_ids_template =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        bidCurrency: "USD",
        adComponents: [
          "adComponent.com/comp?id=1",
          "adComponent.com/comp?id=2"
        ],
        allowComponentAuction: false,
        buyerReportingId: "%s",
        buyerAndSellerReportingId: "%s",
        selectedBuyerAndSellerReportingId: "%s"
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_user_bidding_signals_template =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      // Test that user bidding signals are present
      let length = interest_group.userBiddingSignals.length;

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view
    js_code_requiring_parsed_user_bidding_signals_template =
        R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      let ubs = interest_group.userBiddingSignals;
      if ((ubs.someId === 1789) && (ubs.name === "winston")
          && ((ubs.years[0] === 1776) && (ubs.years[1] === 1868))) {
        // Reshaped into an AdWithBid.
        return {
          render: "%s" + interest_group.adRenderIds[0],
          ad: {"arbitraryMetadataField": 1},
          bid: bid,
          allowComponentAuction: false
        };
      }
    }
  )JS_CODE";

constexpr absl::string_view js_code_requiring_trusted_bidding_signals_template =
    R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"tbsLength": Object.keys(trusted_bidding_signals).length},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view
    js_code_requiring_trusted_bidding_signals_keys_template =
        R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"tbskLength": interest_group.trustedBiddingSignalsKeys.length},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view
    js_code_requiring_null_trusted_bidding_signals_template =
        R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      const bid = Math.floor(Math.random() * 10 + 1);

      // Reshaped into an AdWithBid.
      if (trusted_bidding_signals === null) {
        return {
          render: "%s" + interest_group.adRenderIds[0],
          ad: {"tbsEmpty": true},
          bid: bid,
          allowComponentAuction: false
        };
      } else {
        throw new Error('Error: Bidding signals were unexpectedly present!');
      }
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_urls_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
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

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));
      forDebuggingOnly.reportAdAuctionLoss(generateRandomUrl(65538));
      forDebuggingOnly.reportAdAuctionWin(generateRandomUrl(65536));
      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_global_this_debug_urls_template =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      globalThis.forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      globalThis.forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");

      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_throws_exception_with_debug_urls =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin("https://example-dsp.com/debugWin");
      throw new Error('Exception message');
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_logs_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Do a random amount of work to generate the price:
      const bid = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from generateBid");
      console.warn("Warning from generateBid");
      console.error("Error from generateBid");
      // Reshaped into an AdWithBid.
      return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view kJsCodeWithTopLevelSeller = R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Reshaped into an AdWithBid.
      return {
        render: "test",
        ad: { "topLevelSeller" : device_signals["topLevelSeller"] },
        bid: 11,
        allowComponentAuction: true
      };
    }
  )JS_CODE";

constexpr absl::string_view kJsCodeWithComponentBidNotAllowed = R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {
      // Reshaped into an AdWithBid.
      return {
        render: "test",
        ad: { "topLevelSeller" : device_signals["topLevelSeller"] },
        bid: 11,
        allowComponentAuction: false
      };
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_multiple_bids_template = R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {

      // Reshaped into an AdWithBid.
      return [
      {
        render: "render_1",
        ad: {"arbitraryMetadataField": 1},
        bid: 1,
        allowComponentAuction: false
      },
      {
        render: "render_2",
        ad: {"arbitraryMetadataField": 2},
        bid: 2,
        allowComponentAuction: false
      },
      {
        render: "render_3",
        ad: {"arbitraryMetadataField": 3},
        bid: 3,
        allowComponentAuction: false
      },
      {
        render: "render_4",
        ad: {"arbitraryMetadataField": 4},
        bid: 4,
        allowComponentAuction: false
      }
      ];
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_url_map_template = R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {

      forDebuggingOnly.reportAdAuctionLoss(new Map([ ["render_1", "https://render_1.com/debugLoss"],
                                                       ["render_2", "https://render_2.com/debugLoss"],
                                                       ["render_3", "https://render_3.com/debugLoss"],
                                                       ["render_4", "https://render_4.com/debugLoss"]]));
      forDebuggingOnly.reportAdAuctionWin(new Map([ ["render_1", "https://render_1.com/debugWin"],
                                                      ["render_2", "https://render_2.com/debugWin"],
                                                      ["render_3", "https://render_3.com/debugWin"],
                                                      ["render_4", "https://render_4.com/debugWin"]]));

      // Reshaped into an AdWithBid.
      return [
      {
        render: "render_1",
        ad: {"arbitraryMetadataField": 1},
        bid: 1,
        allowComponentAuction: false
      },
      {
        render: "render_2",
        ad: {"arbitraryMetadataField": 2},
        bid: 2,
        allowComponentAuction: false
      },
      {
        render: "render_3",
        ad: {"arbitraryMetadataField": 3},
        bid: 3,
        allowComponentAuction: false
      },
      {
        render: "render_4",
        ad: {"arbitraryMetadataField": 4},
        bid: 4,
        allowComponentAuction: false
      }
      ];
    }
  )JS_CODE";

constexpr absl::string_view js_code_with_debug_url_missing_key_template =
    R"JS_CODE(
    function generateBid(interest_group,
                         auction_signals,
                         buyer_signals,
                         trusted_bidding_signals,
                         device_signals) {

      forDebuggingOnly.reportAdAuctionLoss("https://example-dsp.com/debugLoss");
      forDebuggingOnly.reportAdAuctionWin(new Map([ ["render_1", "https://render_1.com/debugWin"],
                                                    ["render_2", "https://render_2.com/debugWin"]]));

      // Reshaped into an AdWithBid.
      return [
      {
        render: "render_1",
        ad: {"arbitraryMetadataField": 1},
        bid: 1,
        allowComponentAuction: false
      },
      {
        render: "render_2",
        ad: {"arbitraryMetadataField": 2},
        bid: 2,
        allowComponentAuction: false
      },
      {
        render: "render_3",
        ad: {"arbitraryMetadataField": 3},
        bid: 3,
        allowComponentAuction: false
      }
      ];
    }
  )JS_CODE";

SignalBucket GetExpectedSignalBucket() {
  BucketOffset bucket_offset;
  bucket_offset.add_value(1);  // Add first 64-bit value
  bucket_offset.add_value(0);  // Add second 64-bit value
  bucket_offset.set_is_negative(false);

  SignalBucket signal_bucket;
  signal_bucket.set_base_value(BASE_VALUE_WINNING_BID);
  signal_bucket.set_scale(2.0);
  *signal_bucket.mutable_offset() = bucket_offset;

  return signal_bucket;
}

SignalValue GetExpectedSignalValue() {
  SignalValue signal_value;
  signal_value.set_base_value(BASE_VALUE_HIGHEST_SCORING_OTHER_BID);
  signal_value.set_scale(3.0);
  signal_value.set_offset(2);
  return signal_value;
}

void SetupMockCryptoClientWrapper(MockCryptoClientWrapper& crypto_client) {
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillRepeatedly(
          [](const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
             const std::string& plaintext_payload) {
            google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
                hpke_encrypt_response;
            hpke_encrypt_response.set_secret(kSecret);
            hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
            hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
                plaintext_payload);
            return hpke_encrypt_response;
          });

  // Mock the HpkeDecrypt() call on the crypto_client. This is used by the
  // service to decrypt the incoming request.
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly([](const server_common::PrivateKey& private_key,
                         const std::string& ciphertext) {
        google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
            hpke_decrypt_response;
        *hpke_decrypt_response.mutable_payload() = ciphertext;
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
            *data.mutable_ciphertext() = plaintext_payload;
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });
}

// The following is a base64 encoded string of wasm binary output
// that exports a function with the following definition:
// int plusOne(int x)
constexpr absl::string_view base64_wasm_plus_one =
    "AGFzbQEAAAABhoCAgAABYAF/"
    "AX8DgoCAgAABAASEgICAAAFwAAAFg4CAgAABAAEGgYCAgAAAB5SAgIAAAgZtZW1vcnkCAAdwbH"
    "VzT25lAAAKjYCAgAABh4CAgAAAIABBAWoL";
constexpr absl::string_view js_code_runs_wasm_helper = R"JS_CODE(
function generateBid( interest_group,
                      auction_signals,
                      buyer_signals,
                      trusted_bidding_signals,
                      device_signals) {
  const instance = new WebAssembly.Instance(device_signals.wasmHelper);

  //Reshaped into an AdWithBid.
  return {
    render: "%s" + interest_group.adRenderIds[0],
    ad: {"tbsLength": Object.keys(trusted_bidding_signals).length},
    bid: instance.exports.plusOne(0),
    allowComponentAuction: false
  };
}
)JS_CODE";

void SetupV8Dispatcher(V8Dispatcher* dispatcher, absl::string_view adtech_js,
                       std::string adtech_wasm = "",
                       bool enable_private_aggregate_reporting = false) {
  ASSERT_TRUE(dispatcher->Init().ok());
  BuyerCodeWrapperConfig wrapper_config = {
      .ad_tech_wasm = std::move(adtech_wasm),
      .enable_private_aggregate_reporting = enable_private_aggregate_reporting};
  std::string wrapper_blob = GetBuyerWrappedCode(adtech_js, wrapper_config);
  ASSERT_TRUE(dispatcher
                  ->LoadSync(kProtectedAudienceGenerateBidBlobVersion,
                             std::move(wrapper_blob))
                  .ok());
}

absl::StatusOr<GenerateBidsRequest::GenerateBidsRawRequest>
BuildGenerateBidsRequestFromBrowser(
    absl::flat_hash_map<std::string, std::vector<std::string>>*
        interest_group_to_ad,
    int desired_bid_count = 5, bool set_enable_debug_reporting = false,
    bool enable_adtech_code_logging = false,
    int multi_bid_limit = kTestMultiBidLimit) {
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  raw_request.set_enable_debug_reporting(set_enable_debug_reporting);
  raw_request.set_data_version(kDataVersion);
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeARandomInterestGroupForBiddingFromBrowser();
    interest_group_to_ad->try_emplace(
        interest_group.name(),
        std::vector<std::string>(interest_group.ad_render_ids().begin(),
                                 interest_group.ad_render_ids().end()));
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        std::move(interest_group);
  }
  if (enable_adtech_code_logging) {
    raw_request.mutable_consented_debug_config()->set_token(kTestConsentToken);
    raw_request.mutable_consented_debug_config()->set_is_consented(true);
  }
  raw_request.set_multi_bid_limit(multi_bid_limit);
  return raw_request;
}

class GenerateBidsReactorIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();

    // initialize
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
    server_common::log::ServerToken(kTestConsentToken);

    TrustedServersConfigClient config_client({});
    config_client.SetOverride(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(
        config_client, /* public_key_fetcher= */ nullptr);
    SetupMockCryptoClientWrapper(*crypto_client_);
  }

  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  BiddingServiceRuntimeConfig bidding_service_runtime_config_;
};

TEST_F(GenerateBidsReactorIntegrationTest, GeneratesBidsByInterestGroupCode) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, absl::StrFormat(js_code_template,
                                                 kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto req = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  ASSERT_TRUE(req.ok()) << req.status();
  request.set_request_ciphertext(req->SerializeAsString());

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_EQ(ad_with_bid.bid_currency(), "USD");
    std::string expected_render_url =
        kAdRenderUrlPrefixForTest +
        interest_group_to_ad.at(ad_with_bid.interest_group_name()).at(0);
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    EXPECT_EQ(ad_with_bid.ad_components_size(), 2);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, ReportingIdsSetInResponse) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  std::string test_buyer_reporting_id = "testBuyerReportingId";
  std::string test_bas_reporting_id = "testBuyerAndSellerReportingId";
  std::string test_selected_reporting_id =
      "testSelectedBuyerAndSellerReportingId";
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_with_reporting_ids_template,
                      kAdRenderUrlPrefixForTest, test_buyer_reporting_id,
                      test_bas_reporting_id, test_selected_reporting_id));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto req = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  ASSERT_TRUE(req.ok()) << req.status();
  request.set_request_ciphertext(req->SerializeAsString());

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_EQ(ad_with_bid.buyer_reporting_id(), test_buyer_reporting_id);
    EXPECT_EQ(ad_with_bid.buyer_and_seller_reporting_id(),
              test_bas_reporting_id);
    EXPECT_EQ(ad_with_bid.selected_buyer_and_seller_reporting_id(),
              test_selected_reporting_id);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsWithParsedUserBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_parsed_user_bidding_signals_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto req = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  ASSERT_TRUE(req.ok()) << req.status();
  request.set_request_ciphertext(req->SerializeAsString());

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url =
        kAdRenderUrlPrefixForTest +
        interest_group_to_ad.at(ad_with_bid.interest_group_name()).at(0);
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, ReceivesTrustedBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_trusted_bidding_signals_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto req = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  ASSERT_TRUE(req.ok()) << req.status();
  auto raw_request = *std::move(req);
  request.set_request_ciphertext(raw_request.SerializeAsString());
  ASSERT_EQ(raw_request.interest_group_for_bidding_size(), 5);
  for (const auto& ig : raw_request.interest_group_for_bidding()) {
    ASSERT_GT(ig.trusted_bidding_signals().length(), 0);
  }

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    ASSERT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbsLength") !=
                ad_with_bid.ad().struct_value().fields().end());
    // One signal key per IG (priority vector not passed to generateBid()).
    EXPECT_EQ(
        ad_with_bid.ad().struct_value().fields().at("tbsLength").number_value(),
        1);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, ReceivesTrustedBiddingSignalsKeys) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_trusted_bidding_signals_keys_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto req = BuildGenerateBidsRequestFromBrowser(&interest_group_to_ad);
  ASSERT_TRUE(req.ok()) << req.status();
  auto raw_request = *std::move(req);
  request.set_request_ciphertext(raw_request.SerializeAsString());
  ASSERT_GT(raw_request.interest_group_for_bidding(0)
                .trusted_bidding_signals_keys_size(),
            0);

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    ASSERT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbskLength") !=
                ad_with_bid.ad().struct_value().fields().end());
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("tbskLength")
                  .number_value(),
              raw_request.interest_group_for_bidding(0)
                  .trusted_bidding_signals_keys_size());
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsDespiteNullTrustedBiddingSignalsWhenNotRequired) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_null_trusted_bidding_signals_template,
                      kAdRenderUrlPrefixForTest));

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  auto interest_group = MakeARandomInterestGroupForBiddingFromBrowser();
  // Important: Set bidding signals to null to maintain parity since this is
  // what Chrome does when KV lookup fails.
  interest_group.set_trusted_bidding_signals("null");
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      std::move(interest_group);
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_TRUE(ad_with_bid.ad().struct_value().fields().find("tbsEmpty") !=
                ad_with_bid.ad().struct_value().fields().end());
  }
}

/*
 * This test exists to demonstrate that if an AdTech's script expects a
 * property to be present in the interest group, but that property is set to a
 * value which the protobuf serializer serializes to an empty string, then that
 * property WILL BE OMITTED from the serialized interest_group passed to the
 * generateBid() script, and the script will CRASH.
 * It so happens that the SideLoad Data provider provided such a value for
 * interestGroup.userBiddingSignals when userBiddingSignals are not present,
 * and the generateBid() script with which we test requires the
 * .userBiddingSignals property to be present and crashes when it is absent.
 */
TEST_F(GenerateBidsReactorIntegrationTest,
       FailsToGenerateBidsWhenMissingUserBiddingSignals) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(
      &dispatcher,
      absl::StrFormat(js_code_requiring_user_bidding_signals_template,
                      kAdRenderUrlPrefixForTest),
      "");

  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  auto interest_group = MakeARandomInterestGroupForBidding(false, true);
  // Important: Clear user bidding signals.
  ASSERT_TRUE(interest_group.user_bidding_signals().empty());
  *raw_request.mutable_interest_group_for_bidding()->Add() =
      std::move(interest_group);
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  ASSERT_TRUE(response.IsInitialized());
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  ASSERT_TRUE(raw_response.IsInitialized());

  // All instances of the script should have crashed; no bids should have been
  // generated.
  ASSERT_EQ(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest, GeneratesBidsFromDevice) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, absl::StrFormat(js_code_template,
                                                 kAdRenderUrlPrefixForTest));
  int desired_bid_count = 1;
  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  raw_request.set_data_version(kDataVersion);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  for (int i = 0; i < desired_bid_count; i++) {
    auto interest_group = MakeAnInterestGroupForBiddingSentFromDevice();
    ASSERT_EQ(interest_group.ad_render_ids_size(), 2);
    interest_group_to_ad.try_emplace(
        interest_group.name(),
        std::vector<std::string>(interest_group.ad_render_ids().begin(),
                                 interest_group.ad_render_ids().end()));
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        std::move(interest_group);
    raw_request.set_multi_bid_limit(kTestMultiBidLimit);
    // This fails in production, the user Bidding Signals are not being set.
    // use logging to figure out why.
  }
  *request.mutable_request_ciphertext() = raw_request.SerializeAsString();

  GenerateBidsResponse response;
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager_),
                         std::move(crypto_client_),
                         bidding_service_runtime_config_,
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, &response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 1);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    std::string expected_render_url = absl::StrCat(
        kAdRenderUrlPrefixForTest,
        interest_group_to_ad.at(ad_with_bid.interest_group_name()).at(0));
    EXPECT_GT(ad_with_bid.render().length(), 0);
    EXPECT_EQ(ad_with_bid.render(), expected_render_url);
    // Expected false because it is expected to be present and was manually set
    // to false.
    EXPECT_FALSE(ad_with_bid.allow_component_auction());
    ASSERT_TRUE(ad_with_bid.ad().has_struct_value());
    EXPECT_EQ(ad_with_bid.ad().struct_value().fields_size(), 1);
    EXPECT_EQ(ad_with_bid.ad()
                  .struct_value()
                  .fields()
                  .at("arbitraryMetadataField")
                  .number_value(),
              1.0);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

struct GenerateBidHelperConfig {
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = false;
  bool enable_adtech_code_logging = false;
  std::string wasm_blob = "";
  int desired_bid_count = 5;
  bool component_auction = false;
  bool enable_private_aggregate_reporting = false;
  int multi_bid_limit = kTestMultiBidLimit;
  int per_adtech_paapi_contributions_limit = 100;
};

void GenerateBidCodeWrapperTestHelper(
    GenerateBidsResponse* response, absl::string_view js_blob,
    const GenerateBidHelperConfig& test_config) {
  grpc::CallbackServerContext context;
  V8Dispatcher dispatcher;
  V8DispatchClient client(dispatcher);
  SetupV8Dispatcher(&dispatcher, js_blob, test_config.wasm_blob,
                    test_config.enable_private_aggregate_reporting);
  GenerateBidsRequest request;
  request.set_key_id(kKeyId);
  absl::flat_hash_map<std::string, std::vector<std::string>>
      interest_group_to_ad;
  auto req = BuildGenerateBidsRequestFromBrowser(
      &interest_group_to_ad, test_config.desired_bid_count,
      test_config.enable_debug_reporting,
      test_config.enable_adtech_code_logging, test_config.multi_bid_limit);
  ASSERT_TRUE(req.ok()) << req.status();
  if (test_config.component_auction) {
    req.value().set_top_level_seller(kTopLevelSeller);
  }
  *request.mutable_request_ciphertext() = req->SerializeAsString();

  std::unique_ptr<MockCryptoClientWrapper> crypto_client =
      std::make_unique<MockCryptoClientWrapper>();
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  SetupMockCryptoClientWrapper(*crypto_client);
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager = CreateKeyFetcherManager(
          config_client, /* public_key_fetcher= */ nullptr);

  BiddingServiceRuntimeConfig runtime_config{
      .enable_buyer_debug_url_generation =
          test_config.enable_buyer_debug_url_generation,
      .enable_private_aggregate_reporting =
          test_config.enable_private_aggregate_reporting,
      .per_adtech_paapi_contributions_limit =
          test_config.per_adtech_paapi_contributions_limit};
  BiddingService service(GetProtectedAudienceV8ReactorFactory(client),
                         std::move(key_fetcher_manager),
                         std::move(crypto_client), std::move(runtime_config),
                         GetProtectedAppSignalsV8ReactorFactory(client));
  auto result = StartLocalService(&service);
  auto stub = CreateServiceStub<Bidding>(result.port);
  grpc::ClientContext client_context;
  grpc::Status status = stub->GenerateBids(&client_context, request, response);
  EXPECT_EQ(status.error_code(), grpc::StatusCode::OK)
      << server_common::ToAbslStatus(status);
}

TEST_F(GenerateBidsReactorIntegrationTest, BuyerDebugUrlGenerationDisabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = false;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation};
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      test_config);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, EventLevelDebugReportingDisabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation};

  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrls) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation};
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(),
              "https://example-dsp.com/debugWin");
    EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(),
              "https://example-dsp.com/debugLoss");
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       ReturnsNumericPAAPIBucketAndValueInTheResponse) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .enable_private_aggregate_reporting = true,
      .per_adtech_paapi_contributions_limit = 1};
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(kBuyerBaseCodeForPrivateAggregation,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    auto& contributions = ad_with_bid.private_aggregation_contributions();
    EXPECT_EQ(contributions.size(), 4);
    EXPECT_EQ(contributions.at(0).event().event_type(),
              EventType::EVENT_TYPE_WIN);
    int win_count = 0;
    int loss_count = 0;
    int always_count = 0;
    int custom_count = 0;
    for (auto& contribution : contributions) {
      switch (contribution.event().event_type()) {
        case EventType::EVENT_TYPE_WIN:
          win_count++;
          break;
        case EventType::EVENT_TYPE_LOSS:
          loss_count++;
          break;
        case EventType::EVENT_TYPE_ALWAYS:
          always_count++;
          break;
        case EventType::EVENT_TYPE_CUSTOM:
          custom_count++;
        default:
          break;
      }
      ASSERT_EQ(contribution.bucket().bucket_128_bit().bucket_128_bits_size(),
                2)
          << "bucket_128_bit has not been set correctly in the contribution";
      EXPECT_EQ(contribution.bucket().bucket_128_bit().bucket_128_bits().at(0),
                100)
          << "bucket_128_bit has not been set in the contribution";
      EXPECT_EQ(contribution.value().int_value(), 200)
          << "int_value has not been set correctly in the contribution";
    }
    EXPECT_EQ(win_count, 1);
    EXPECT_EQ(loss_count, 1);
    EXPECT_EQ(always_count, 1);
    EXPECT_EQ(custom_count, 1);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       ReturnsPAAPISignalBucketAndSignalValueInTheResponse) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .enable_private_aggregate_reporting = true};
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(kBuyerBaseCodeWithSignalValueAndSignalBucket,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
    auto& contributions = ad_with_bid.private_aggregation_contributions();
    EXPECT_EQ(contributions.size(), 1);
    EXPECT_EQ(contributions.at(0).event().event_type(),
              EventType::EVENT_TYPE_WIN);
    ASSERT_TRUE(contributions.at(0).bucket().has_signal_bucket())
        << "SignalBucket has not been set in the contribution";
    ASSERT_TRUE(contributions.at(0).value().has_extended_value())
        << "SignalValue has not been set in the contribution";
    google::protobuf::util::MessageDifferencer diff;
    std::string diff_output;
    diff.ReportDifferencesToString(&diff_output);
    EXPECT_TRUE(diff.Compare(contributions.at(0).bucket().signal_bucket(),
                             GetExpectedSignalBucket()))
        << diff_output;
    EXPECT_TRUE(diff.Compare(contributions.at(0).value().extended_value(),
                             GetExpectedSignalValue()))
        << diff_output;
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsDoesNotReturnLargeDebugReportingUrls) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation};
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_very_large_debug_urls,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
    EXPECT_TRUE(ad_with_bid.has_debug_report_urls());
    EXPECT_GT(ad_with_bid.debug_report_urls().auction_debug_win_url().length(),
              0);
    EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url().length(),
              0);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrlsMaxSize) {
  long debug_urls_max_size = 3000L * 1024L;
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  int desired_bid_count = 100;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .desired_bid_count = desired_bid_count};

  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_very_large_debug_urls,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());

  EXPECT_GT(raw_response.bids_size(), 0);
  long actual_debug_urls_size = 0;
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
    if (ad_with_bid.has_debug_report_urls()) {
      actual_debug_urls_size +=
          ad_with_bid.debug_report_urls().auction_debug_win_url().length() +
          ad_with_bid.debug_report_urls().auction_debug_loss_url().length();
    }
  }
  EXPECT_GE(debug_urls_max_size, actual_debug_urls_size);
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GeneratesBidsReturnDebugReportingUrlsWithGlobalThisMethodCalls) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
  };
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_global_this_debug_urls_template,
                      kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
    EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(),
              "https://example-dsp.com/debugWin");
    EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(),
              "https://example-dsp.com/debugLoss");
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       NoDebugReportingUrlsSentWhenScriptCrashes) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
  };
  GenerateBidCodeWrapperTestHelper(
      &response, js_code_throws_exception_with_debug_urls, test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest,
       NoGenerateBidsResponseIfNoDebugUrlsAndScriptCrashes) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
  };
  GenerateBidCodeWrapperTestHelper(&response, js_code_throws_exception,
                                   test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GenerateBidsReturnsSuccessFullyWithLoggingEnabled) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = false;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .enable_adtech_code_logging = true};
  GenerateBidCodeWrapperTestHelper(
      &response,
      absl::StrFormat(js_code_with_logs_template, kAdRenderUrlPrefixForTest),
      test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest,
       GenerateBidsReturnsSuccessWithWasmHelperCall) {
  GenerateBidsResponse response;
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = false;

  std::string raw_wasm_bytes;
  ASSERT_TRUE(absl::Base64Unescape(base64_wasm_plus_one, &raw_wasm_bytes));
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = enable_debug_reporting,
      .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
      .enable_adtech_code_logging = true,
      .wasm_blob = raw_wasm_bytes};
  GenerateBidCodeWrapperTestHelper(&response, js_code_runs_wasm_helper,
                                   test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_EQ(ad_with_bid.bid(), 1);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, ParsesFieldsForComponentAuction) {
  GenerateBidsResponse response;
  GenerateBidHelperConfig test_config = {.component_auction = true};
  GenerateBidCodeWrapperTestHelper(&response, kJsCodeWithTopLevelSeller,
                                   test_config);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_EQ(ad_with_bid.allow_component_auction(), true);
    EXPECT_EQ(ad_with_bid.data_version(), kDataVersion);
    const auto& top_level_seller_it =
        ad_with_bid.ad().struct_value().fields().find("topLevelSeller");
    ASSERT_TRUE(top_level_seller_it !=
                ad_with_bid.ad().struct_value().fields().end());
    EXPECT_EQ(top_level_seller_it->second.string_value(), kTopLevelSeller);
  }
}

TEST_F(GenerateBidsReactorIntegrationTest,
       FiltersUnallowedAdsForComponentAuction) {
  GenerateBidsResponse response;
  GenerateBidHelperConfig test_config = {.component_auction = true};
  GenerateBidCodeWrapperTestHelper(&response, kJsCodeWithComponentBidNotAllowed,
                                   test_config);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_EQ(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest, DropsAllBidsIfOverMultiBidLimit) {
  GenerateBidsResponse response;
  GenerateBidHelperConfig test_config = {.desired_bid_count = 1,
                                         .multi_bid_limit = 3};
  GenerateBidCodeWrapperTestHelper(
      &response, js_code_with_multiple_bids_template, test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  EXPECT_EQ(raw_response.bids_size(), 0);
}

TEST_F(GenerateBidsReactorIntegrationTest, ReturnsBidsArrayWithDebugURL) {
  GenerateBidsResponse response;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = true,
      .enable_buyer_debug_url_generation = true,
      .enable_adtech_code_logging = true,
      .desired_bid_count = 1};
  GenerateBidCodeWrapperTestHelper(
      &response, js_code_with_debug_url_map_template, test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    EXPECT_GT(ad_with_bid.bid(), 0);
    EXPECT_TRUE(absl::StrContains(
        ad_with_bid.debug_report_urls().auction_debug_win_url(), "debugWin"));
    EXPECT_TRUE(absl::StrContains(
        ad_with_bid.debug_report_urls().auction_debug_win_url(),
        ad_with_bid.render()));
    EXPECT_TRUE(absl::StrContains(
        ad_with_bid.debug_report_urls().auction_debug_loss_url(), "debugLoss"));
    EXPECT_TRUE(absl::StrContains(
        ad_with_bid.debug_report_urls().auction_debug_loss_url(),
        ad_with_bid.render()));
  }
}

TEST_F(GenerateBidsReactorIntegrationTest, DoesNotAttachUndefinedDebugURLs) {
  GenerateBidsResponse response;
  GenerateBidHelperConfig test_config = {
      .enable_debug_reporting = true,
      .enable_buyer_debug_url_generation = true,
      .enable_adtech_code_logging = true,
      .desired_bid_count = 1};
  GenerateBidCodeWrapperTestHelper(
      &response, js_code_with_debug_url_missing_key_template, test_config);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));
  EXPECT_GT(raw_response.bids_size(), 0);
  for (const auto& ad_with_bid : raw_response.bids()) {
    // If the given debug url is a string but multiple bids are generated
    // in one generateBid() call, that url is not attached.
    EXPECT_TRUE(
        ad_with_bid.debug_report_urls().auction_debug_loss_url().empty());

    if (ad_with_bid.bid() == 3) {
      // If the given debug url is a map but matching render key does not exist,
      // that url is not attached.
      EXPECT_TRUE(
          ad_with_bid.debug_report_urls().auction_debug_win_url().empty());
    } else {
      // If one of the debug urls is a map and matching render key exists,
      // that url gets attached.
      EXPECT_TRUE(absl::StrContains(
          ad_with_bid.debug_report_urls().auction_debug_win_url(), "debugWin"));
      EXPECT_TRUE(absl::StrContains(
          ad_with_bid.debug_report_urls().auction_debug_win_url(),
          ad_with_bid.render()));
    }
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
