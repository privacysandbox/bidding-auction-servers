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

#include "tools/secure_invoke/secure_invoke_lib.h"

#include <gmock/gmock-matchers.h>

#include <memory>
#include <utility>

#include <include/gmock/gmock-actions.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/buyer_frontend_service.h"
#include "services/buyer_frontend_service/util/buyer_frontend_test_utils.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/common/test/utils/service_utils.h"
#include "services/common/test/utils/test_init.h"
#include "tools/secure_invoke/flags.h"

using ::privacy_sandbox::bidding_auction_servers::HpkeKeyset;
using ::testing::HasSubstr;

constexpr char kClientIp[] = "104.133.126.32";
constexpr char kUserAgent[] =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36";
constexpr char kAcceptLanguage[] = "en-US,en;q=0.9";
constexpr char kSampleGetBidRequest[] = R"JSON({
   "auctionSignals" : "{\"maxFloorCpmUsdMicros\":\"6250\"}",
   "buyerInput" : {
      "interestGroups" : [
         {
            "biddingSignalsKeys" : [
               "1j473877681"
            ],
            "browserSignals" : {
               "bidCount" : "17",
               "joinCount" : "3",
               "prevWins" : "[]"
            },
            "name" : "nike shoe",
            "userBiddingSignals" : "[[]]"
         }
      ]
   },
   "buyerSignals" : "[[[[\"Na6y7V9RU0A\",[-0.006591797,0.052001953]]]]]",
      "logContext" : {
      "generationId" : "hardcoded-uuid"
   },
   "publisherName" : "example.com",
   "seller" : "https://sellerorigin.com"
})JSON";

constexpr char kSampleComponentGetBidRequest[] = R"JSON({
   "auctionSignals" : "{\"maxFloorCpmUsdMicros\":\"6250\"}",
   "buyerInput" : {
      "interestGroups" : [
         {
            "biddingSignalsKeys" : [
               "1j473877681"
            ],
            "browserSignals" : {
               "bidCount" : "17",
               "joinCount" : "3",
               "prevWins" : "[]"
            },
            "name" : "nike shoe",
            "userBiddingSignals" : "[[]]"
         }
      ]
   },
   "buyerSignals" : "[[[[\"Na6y7V9RU0A\",[-0.006591797,0.052001953]]]]]",
      "logContext" : {
      "generationId" : "hardcoded-uuid"
   },
   "publisherName" : "example.com",
   "seller" : "https://sellerorigin.com",
   "top_level_seller": "https://top-level-seller.com"
})JSON";

void SetUpOptionFlags(int port) {
  absl::SetFlag(&FLAGS_host_addr, absl::StrFormat("localhost:%d", port));
  absl::SetFlag(&FLAGS_client_ip, kClientIp);
  absl::SetFlag(&FLAGS_client_user_agent, kUserAgent);
  absl::SetFlag(&FLAGS_client_accept_language, kAcceptLanguage);
}

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char valid_bidding_signals[] =
    R"JSON({"keys":{"keyA":["member1","member2"]}})JSON";

using ::privacy_sandbox::bidding_auction_servers::SelectAdRequest;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::HasSubstr;

class SecureInvokeLib : public testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    absl::SetFlag(&FLAGS_json_input_str, "{}");
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<GetBidsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);

    TrustedServersConfigClient config_client({});
    config_client.SetOverride(kTrue, TEST_MODE);
    config_client.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
    key_fetcher_manager_ = CreateKeyFetcherManager(
        config_client, CreatePublicKeyFetcher(config_client));

    SetupBiddingProviderMock(
        /*provider=*/*bidding_signals_provider_,
        /*bidding_signals_value=*/valid_bidding_signals,
        /*repeated_get_allowed=*/false,
        /*server_error_to_return=*/std::nullopt,
        /*match_any_params_any_times=*/true);

    GTEST_FLAG_SET(death_test_style, "threadsafe");
  }

  GetBidsRequest request_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_ =
      CreateCryptoClient();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  GetBidsConfig get_bids_config_ = {
      .is_protected_audience_enabled = true,
  };
  BiddingServiceClientConfig bidding_service_client_config_;
  std::unique_ptr<MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>
      bidding_signals_provider_ = std::make_unique<
          MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>();
  const HpkeKeyset default_keyset_ = HpkeKeyset{};
};

TEST_F(SecureInvokeLib, RequestToBfeNeedsClientIp) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  EXPECT_DEATH(auto unused = SendRequestToBfe(default_keyset_, false),
               "Client IP must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsServerAddress) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  EXPECT_DEATH(auto unused = SendRequestToBfe(default_keyset_, false),
               "BFE host address must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsUserAgent) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  absl::SetFlag(&FLAGS_client_ip, kClientIp);
  // Setting empty user agent would cause the validation failure.
  absl::SetFlag(&FLAGS_client_user_agent, "");
  EXPECT_DEATH(auto unused = SendRequestToBfe(default_keyset_, false),
               "User Agent must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsAcceptLanguage) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  absl::SetFlag(&FLAGS_client_ip, kClientIp);
  // Setting empty client accept language would cause the validation failure.
  absl::SetFlag(&FLAGS_client_accept_language, "");
  EXPECT_DEATH(auto unused = SendRequestToBfe(default_keyset_, false),
               "Accept Language must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeReturnsAResponse) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<BuyerFrontEnd::StubInterface> stub =
      CreateServiceStub<BuyerFrontEnd>(start_service_result.port);
  SetUpOptionFlags(start_service_result.port);
  absl::SetFlag(&FLAGS_json_input_str, kSampleGetBidRequest);

  // Verifies that the encrypted request makes it to BFE and the response comes
  // back. Empty response is expected because trusted bidding signals for IG
  // are empty.
  auto status = SendRequestToBfe(default_keyset_, false, std::move(stub));
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(SecureInvokeLib, RequestToBfeReturnsAResponseWithDebugReportingEnabled) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  bool enable_debug_reporting = true;
  std::unique_ptr<BuyerFrontEnd::StubInterface> stub =
      CreateServiceStub<BuyerFrontEnd>(start_service_result.port);
  SetUpOptionFlags(start_service_result.port);
  absl::SetFlag(&FLAGS_json_input_str, kSampleGetBidRequest);

  // Verifies that the encrypted request makes it to BFE and the response comes
  // back. Empty response is expected because trusted bidding signals for IG
  // are empty.
  auto status = SendRequestToBfe(default_keyset_, enable_debug_reporting,
                                 std::move(stub));
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(SecureInvokeLib, IncludesTopLevelSellerInBfeInput) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  bool enable_debug_reporting = true;
  std::unique_ptr<BuyerFrontEnd::StubInterface> stub =
      CreateServiceStub<BuyerFrontEnd>(start_service_result.port);
  SetUpOptionFlags(start_service_result.port);
  absl::SetFlag(&FLAGS_json_input_str, kSampleComponentGetBidRequest);

  // Verifies that the encrypted request makes it to BFE and the response comes
  // back. Empty response is expected because trusted bidding signals for IG
  // are empty.
  auto status = SendRequestToBfe(default_keyset_, enable_debug_reporting,
                                 std::move(stub));
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsValidKey) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<BuyerFrontEnd::StubInterface> stub =
      CreateServiceStub<BuyerFrontEnd>(start_service_result.port);
  SetUpOptionFlags(start_service_result.port);
  absl::SetFlag(&FLAGS_json_input_str, kSampleGetBidRequest);

  // When we pass a malformed key, we expect an error from the client.
  const std::string invalid_key =
      "rvJwF4YQi1hZLWMcGbDf9uGN2jQInZvtHPJsgTUewQY=";
  EXPECT_NE(invalid_key, HpkeKeyset{}.public_key);
  EXPECT_DEATH(
      auto unused = SendRequestToBfe(HpkeKeyset{.public_key = invalid_key},
                                     false, std::move(stub)),
      "Encryption Failure.");
}

TEST_F(SecureInvokeLib, UsesKeyForBfeEncryption) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      get_bids_config_};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<BuyerFrontEnd::StubInterface> stub =
      CreateServiceStub<BuyerFrontEnd>(start_service_result.port);
  SetUpOptionFlags(start_service_result.port);
  absl::SetFlag(&FLAGS_json_input_str, kSampleGetBidRequest);

  // The BFE expects the key to be the default public key if using TEST_MODE =
  // true. When we pass a bogus key, we expect an error from the server.
  const std::string unrecognized_key =
      "aef2701786108b58592d631c19b0dff6e18dda34089d9bed1cf26c81351ec106";
  EXPECT_NE(unrecognized_key, HpkeKeyset{}.public_key);
  auto status = SendRequestToBfe(HpkeKeyset{.public_key = unrecognized_key},
                                 false, std::move(stub));
  EXPECT_FALSE(status.ok()) << status;
  EXPECT_THAT(status.message(), HasSubstr("Malformed request ciphertext"))
      << status;
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
