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

#include <memory>
#include <utility>

#include <gmock/gmock-matchers.h>
#include <include/gmock/gmock-actions.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/buyer_frontend_service/buyer_frontend_service.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/service_utils.h"

constexpr char kSfe[] = "SFE";
constexpr char kBfe[] = "BFE";
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
   "seller" : "https://securepubads.g.doubleclick.net"
})JSON";

using ::privacy_sandbox::bidding_auction_servers::SelectAdRequest;

ABSL_FLAG(std::string, input_file, "",
          "The full path to the unencrypted input request");

ABSL_FLAG(std::string, input_format, "JSON",
          "The format of the input specified in the input file");

ABSL_FLAG(std::string, json_input_str, "{}",
          "The unencrypted JSON request to be used");

ABSL_FLAG(std::string, op, "",
          "The operation to be performed - invoke/encrypt.");

ABSL_FLAG(std::string, host_addr, "",
          "The Address for the SellerFrontEnd server to be invoked.");

ABSL_FLAG(std::string, client_ip, "",
          "The IP for the B&A client to be forwarded to KV servers.");

ABSL_FLAG(std::string, client_accept_language, kAcceptLanguage,
          "The accept-language header for the B&A client to be forwarded to KV "
          "servers.");

ABSL_FLAG(
    std::string, client_user_agent, kUserAgent,
    "The user-agent header for the B&A client to be forwarded to KV servers.");

ABSL_FLAG(std::string, client_type, "",
          "Client type (browser or android) to use for request (defaults to "
          "browser)");

ABSL_FLAG(bool, insecure, false,
          "Set to true to send a request to an SFE server running with "
          "insecure credentials");

ABSL_FLAG(std::string, target_service, kSfe,
          "Service name to which the request must be sent to.");

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::_;
using ::testing::AnyNumber;

class SecureInvokeLib : public testing::Test {
 protected:
  void SetUp() override {
    server_common::metric::ServerConfig config_proto;
    config_proto.set_mode(server_common::metric::ServerConfig::PROD);
    metric::BfeContextMap(
        server_common::metric::BuildDependentConfig(config_proto))
        ->Get(&request_);

    TrustedServersConfigClient config_client({});
    config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_client.SetFlagForTest(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(config_client);

    EXPECT_CALL(*bidding_signals_provider_, Get(_, _, _))
        .Times(AnyNumber())
        .WillOnce([](const BiddingSignalsRequest& bidding_signals_request,
                     auto on_done, absl::Duration timeout) {
          std::move(on_done)(std::make_unique<BiddingSignals>());
        });

    GTEST_FLAG_SET(death_test_style, "threadsafe");
  }

  GetBidsRequest request_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_ =
      CreateCryptoClient();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  GetBidsConfig get_bids_config_ = {
      .encryption_enabled = true,
  };
  BiddingServiceClientConfig bidding_service_client_config_ = {
      .encryption_enabled = true,
  };
  std::unique_ptr<MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>
      bidding_signals_provider_ = std::make_unique<
          MockAsyncProvider<BiddingSignalsRequest, BiddingSignals>>();
};

TEST_F(SecureInvokeLib, RequestToBfeNeedsClientIp) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      std::move(get_bids_config_)};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  EXPECT_DEATH(SendRequestToBfe(), "Client IP must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsServerAddress) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      std::move(get_bids_config_)};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  EXPECT_DEATH(SendRequestToBfe(), "BFE host address must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsUserAgent) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      std::move(get_bids_config_)};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  absl::SetFlag(&FLAGS_client_ip, kClientIp);
  // Setting empty user agent would cause the validation failure.
  absl::SetFlag(&FLAGS_client_user_agent, "");
  EXPECT_DEATH(SendRequestToBfe(), "User Agent must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeNeedsAcceptLanguage) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      std::move(get_bids_config_)};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<Bidding::StubInterface> stub =
      CreateServiceStub<Bidding>(start_service_result.port);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  absl::SetFlag(&FLAGS_client_ip, kClientIp);
  // Setting empty client accept language would cause the validation failure.
  absl::SetFlag(&FLAGS_client_accept_language, "");
  EXPECT_DEATH(SendRequestToBfe(), "Accept Language must be specified");
}

TEST_F(SecureInvokeLib, RequestToBfeReturnsAResponse) {
  BuyerFrontEndService buyer_front_end_service{
      std::move(bidding_signals_provider_), bidding_service_client_config_,
      std::move(key_fetcher_manager_), std::move(crypto_client_),
      std::move(get_bids_config_)};

  auto start_service_result = StartLocalService(&buyer_front_end_service);
  std::unique_ptr<BuyerFrontEnd::StubInterface> stub =
      CreateServiceStub<BuyerFrontEnd>(start_service_result.port);
  absl::SetFlag(&FLAGS_host_addr,
                absl::StrFormat("localhost:%d", start_service_result.port));
  absl::SetFlag(&FLAGS_client_ip, kClientIp);
  absl::SetFlag(&FLAGS_client_user_agent, kUserAgent);
  absl::SetFlag(&FLAGS_client_accept_language, kAcceptLanguage);
  absl::SetFlag(&FLAGS_json_input_str, kSampleGetBidRequest);

  // Verifies that the encrypted request makes it to BFE and the response comes
  // back. Error is expected because test is not setting up the bidding service.
  auto status = SendRequestToBfe(std::move(stub));
  EXPECT_FALSE(status.ok());
  EXPECT_TRUE(absl::StrContains(status.message(),
                                "Execution of GenerateBids request failed"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
