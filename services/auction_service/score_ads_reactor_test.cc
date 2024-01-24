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

#include "services/auction_service/score_ads_reactor.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/benchmarking/score_ads_benchmarking_logger.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/reporting/reporting_helper_test_constants.h"
#include "services/auction_service/score_ads_reactor_test_util.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kSecret[] = "secret";
constexpr char kKeyId[] = "keyid";
constexpr char kTestReportingResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"]})";
constexpr char kTestReportingWinResponseJson[] =
    R"({"reportResultResponse":{"reportResultUrl":"http://reportResultUrl.com","signalsForWinner":"{testKey:testValue}","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"sellerLogs":["testLog"], "sellerErrors":["testLog"], "sellerWarnings":["testLog"],
"reportWinResponse":{"reportWinUrl":"http://reportWinUrl.com","sendReportToInvoked":true,"registerAdBeaconInvoked":true,"interactionReportingUrls":{"click":"http://event.com"}},"buyerLogs":["testLog"], "buyerErrors":["testLog"], "buyerWarnings":["testLog"]})";
constexpr int kTestDesirability = 5;
constexpr char kTestConsentToken[] = "testConsentedToken";
constexpr char kTestEgressFeatures[] = "testEgressFeatures";
constexpr char kTestComponentSeller[] = "testComponentSeller";

using ::google::protobuf::TextFormat;
using ::testing::AnyNumber;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Return;
using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

constexpr char kInterestGroupOwnerOfBarBidder[] = "barStandardAds.com";

void GetTestAdWithBidFoo(AdWithBidMetadata& foo) {
  int number_of_component_ads = 3;
  foo.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd("https://googleads.g.doubleclick.net/td/adfetch/"
               "gda?adg_id=142601302539&cr_id=628073386727&cv_id=0",
               "arbitraryMetadataKey", 2));
  foo.set_render(
      "https://googleads.g.doubleclick.net/td/adfetch/"
      "gda?adg_id=142601302539&cr_id=628073386727&cv_id=0");
  foo.set_bid(2.10);
  foo.set_interest_group_name("foo");
  foo.set_interest_group_owner("https://fooAds.com");
  for (int i = 0; i < number_of_component_ads; i++) {
    foo.add_ad_components(
        absl::StrCat("adComponent.com/foo_components/id=", i));
  }
}

void GetTestAdWithBidSameComponentAsFoo(AdWithBidMetadata& foo) {
  int number_of_component_ads = 3;
  std::string render_url = "fooMeOnceAds.com/render_ad?id=hasFooComp";
  foo.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(render_url, "arbitraryMetadataKey", 2));
  foo.set_render(render_url);
  foo.set_bid(2.10);
  foo.set_interest_group_name("foo");
  foo.set_interest_group_owner("https://fooAds.com");
  for (int i = 0; i < number_of_component_ads; i++) {
    foo.add_ad_components(
        absl::StrCat("adComponent.com/foo_components/id=", i));
  }
}

void GetTestAdWithBidBar(AdWithBidMetadata& bar) {
  int number_of_component_ads = 3;
  std::string render_url = "barStandardAds.com/render_ad?id=bar";
  auto* bar_ad_map = bar.mutable_ad()->mutable_struct_value()->mutable_fields();
  bar_ad_map->try_emplace("renderUrl", MakeAStringValue(render_url));
  bar_ad_map->try_emplace("metadata", MakeAListValue({
                                          MakeAStringValue("140583167746"),
                                          MakeAStringValue("627640802621"),
                                          MakeANullValue(),
                                          MakeAStringValue("18281019067"),
                                      }));

  bar.set_render(render_url);
  bar.set_bid(2);
  bar.set_interest_group_name("ig_bar");
  bar.set_interest_group_owner(kInterestGroupOwnerOfBarBidder);
  for (int i = 0; i < number_of_component_ads; i++) {
    bar.add_ad_components(
        absl::StrCat("adComponent.com/bar_components/id=", i));
  }
}

void GetTestAdWithBidBarbecue(AdWithBidMetadata& ad_with_bid) {
  int number_of_component_ads = 0;
  std::string render_url = "barStandardAds.com/render_ad?id=barbecue";
  auto* bar_ad_map =
      ad_with_bid.mutable_ad()->mutable_struct_value()->mutable_fields();
  bar_ad_map->try_emplace("renderUrl", MakeAStringValue(render_url));
  bar_ad_map->try_emplace("metadata", MakeAListValue({
                                          MakeAStringValue("brisket"),
                                          MakeAStringValue("pulled_pork"),
                                          MakeAStringValue("smoked_chicken"),
                                      }));

  ad_with_bid.set_render(render_url);
  ad_with_bid.set_bid(17.76);
  ad_with_bid.set_interest_group_name("barbecue_lovers");
  ad_with_bid.set_interest_group_owner("barStandardAds.com");
  for (int i = 0; i < number_of_component_ads; i++) {
    ad_with_bid.add_ad_components(
        absl::StrCat("barStandardAds.com/ad_components/id=", i));
  }
}

void GetTestAdWithBidBarbecueWithComponents(AdWithBidMetadata& ad_with_bid) {
  int number_of_component_ads = 1;
  std::string render_url = "barStandardAds.com/render_ad?id=barbecue2";
  auto* bar_ad_map =
      ad_with_bid.mutable_ad()->mutable_struct_value()->mutable_fields();
  bar_ad_map->try_emplace("renderUrl", MakeAStringValue(render_url));
  bar_ad_map->try_emplace("metadata", MakeAListValue({
                                          MakeAStringValue("brisket"),
                                          MakeAStringValue("pulled_pork"),
                                          MakeAStringValue("smoked_chicken"),
                                      }));

  ad_with_bid.set_render(render_url);
  ad_with_bid.set_bid(17.76);
  ad_with_bid.set_interest_group_name("barbecue_lovers");
  ad_with_bid.set_interest_group_owner("barStandardAds.com");
  for (int i = 0; i < number_of_component_ads; i++) {
    ad_with_bid.add_ad_components(
        absl::StrCat("barStandardAds.com/ad_components/id=", i));
  }
}

constexpr char kTestSellerSignals[] = R"json({"seller_signal": "test 1"})json";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test 2"})json";
constexpr char kTestScoringSignals[] = R"json(
  {
    "renderUrls": {
      "https://googleads.g.doubleclick.net/td/adfetch/gda?adg_id=142601302539&cr_id=628073386727&cv_id=0": [
          123
        ],
      "barStandardAds.com/render_ad?id=bar": [
          "barScoringSignalValue1",
          "barScoringSignalValue2"
        ],
      "barStandardAds.com/render_ad?id=barbecue": [
          1689,
          1868
        ],
      "barStandardAds.com/render_ad?id=barbecue2": [
          1689,
          1868
        ],
      "fooMeOnceAds.com/render_ad?id=hasFooComp": [
          1689,
          1868
        ]
    },
    "adComponentRenderUrls": {
      "adComponent.com/foo_components/id=0":["foo0"],
      "adComponent.com/foo_components/id=1":["foo1"],
      "adComponent.com/foo_components/id=2":["foo2"],
      "adComponent.com/bar_components/id=0":["bar0"],
      "adComponent.com/bar_components/id=1":["bar1"],
      "adComponent.com/bar_components/id=2":["bar2"]
    }
  }
)json";
constexpr char kTestProtectedAppSignalsRenderUrl[] =
    "testAppAds.com/render_ad?id=bar";
constexpr char kTestProtectedAppScoringSignals[] = R"json(
  {
    "renderUrls": {
      "testAppAds.com/render_ad?id=bar": ["test_signal"]
    }
  }
)json";
constexpr char kTestPublisherHostname[] = "publisher_hostname";

absl::Status FakeExecute(std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback,
                         std::vector<std::string> json,
                         const bool call_wrapper_method = false,
                         const bool enable_adtech_code_logging = false) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;
  auto response_iterator = json.begin();

  for (const auto& request : batch) {
    if (std::strcmp(request.handler_name.c_str(),
                    kReportingDispatchHandlerFunctionName) != 0) {
      EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
    }
    DispatchResponse dispatch_response = {};
    dispatch_response.resp = *response_iterator++;
    dispatch_response.id = request.id;
    responses.emplace_back(dispatch_response);
  }
  done_callback(responses);
  return absl::OkStatus();
}

namespace {

void BuildRawRequest(const std::vector<AdWithBidMetadata> ads_with_bids_to_add,
                     absl::string_view seller_signals,
                     absl::string_view auction_signals,
                     absl::string_view scoring_signals,
                     absl::string_view publisher_hostname, RawRequest& output,
                     const bool enable_debug_reporting = false,
                     const bool enable_adtech_code_logging = false,
                     absl::string_view top_level_seller = "") {
  for (int i = 0; i < ads_with_bids_to_add.size(); i++) {
    output.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids_to_add[i].interest_group_owner(), kTestBuyerSignals);
    *output.add_ad_bids() = std::move(ads_with_bids_to_add[i]);
  }
  output.set_seller_signals(seller_signals);
  output.set_auction_signals(auction_signals);
  output.set_scoring_signals(scoring_signals);
  output.set_publisher_hostname(publisher_hostname);
  output.set_enable_debug_reporting(enable_debug_reporting);
  if (!top_level_seller.empty()) {
    output.set_top_level_seller(top_level_seller);
  }
  output.mutable_consented_debug_config()->set_is_consented(
      enable_adtech_code_logging);
  output.mutable_consented_debug_config()->set_token(kTestConsentToken);
}

void BuildRawRequestForComponentAuction(
    std::vector<AdWithBidMetadata> ads_with_bids_to_add,
    absl::string_view seller_signals, absl::string_view auction_signals,
    absl::string_view scoring_signals, absl::string_view publisher_hostname,
    RawRequest& output, const bool enable_debug_reporting = false) {
  output.set_top_level_seller(kTestTopLevelSeller);
  BuildRawRequest(ads_with_bids_to_add, seller_signals, auction_signals,
                  scoring_signals, publisher_hostname, output,
                  enable_debug_reporting);
}

}  // namespace

void CheckInputCorrectForFoo(std::vector<std::shared_ptr<std::string>> input) {
  EXPECT_EQ(*input[0], R"JSON({"arbitraryMetadataKey":2})JSON");
  EXPECT_EQ(*input[1], R"JSON(2.100000)JSON");
  EXPECT_EQ(
      *input[2],
      R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
  EXPECT_EQ(
      *input[3],
      R"JSON({"adComponentRenderUrls":{"adComponent.com/foo_components/id=0":["foo0"],"adComponent.com/foo_components/id=1":["foo1"],"adComponent.com/foo_components/id=2":["foo2"]},"renderUrl":{"https://googleads.g.doubleclick.net/td/adfetch/gda?adg_id=142601302539&cr_id=628073386727&cv_id=0":[123]}})JSON");
  EXPECT_EQ(
      *input[4],
      R"JSON({"interestGroupOwner":"https://fooAds.com","topWindowHostname":"publisher_hostname","adComponents":["adComponent.com/foo_components/id=0","adComponent.com/foo_components/id=1","adComponent.com/foo_components/id=2"],"renderUrl":"https://googleads.g.doubleclick.net/td/adfetch/gda?adg_id=142601302539&cr_id=628073386727&cv_id=0"})JSON");
  EXPECT_EQ(*input[5], "{}");
}

void CheckInputCorrectForBar(std::vector<std::shared_ptr<std::string>> input) {
  EXPECT_EQ(*input[0],
            R"JSON(["140583167746","627640802621",null,"18281019067"])JSON");
  EXPECT_EQ(*input[1], R"JSON(2.000000)JSON");
  EXPECT_EQ(
      *input[2],
      R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
  EXPECT_EQ(
      *input[3],
      R"JSON({"adComponentRenderUrls":{"adComponent.com/bar_components/id=0":["bar0"],"adComponent.com/bar_components/id=1":["bar1"],"adComponent.com/bar_components/id=2":["bar2"]},"renderUrl":{"barStandardAds.com/render_ad?id=bar":["barScoringSignalValue1","barScoringSignalValue2"]}})JSON");
  EXPECT_EQ(
      *input[4],
      R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisher_hostname","adComponents":["adComponent.com/bar_components/id=0","adComponent.com/bar_components/id=1","adComponent.com/bar_components/id=2"],"renderUrl":"barStandardAds.com/render_ad?id=bar"})JSON");
  EXPECT_EQ(*input[5], "{}");
}

void CheckInputCorrectForAdWithFooComp(
    std::vector<std::shared_ptr<std::string>> input) {
  EXPECT_EQ(*input[0], R"JSON({"arbitraryMetadataKey":2})JSON");
  EXPECT_EQ(*input[1], R"JSON(2.100000)JSON");
  EXPECT_EQ(
      *input[2],
      R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
  EXPECT_EQ(
      *input[3],
      R"JSON({"adComponentRenderUrls":{"adComponent.com/foo_components/id=0":["foo0"],"adComponent.com/foo_components/id=1":["foo1"],"adComponent.com/foo_components/id=2":["foo2"]},"renderUrl":{"fooMeOnceAds.com/render_ad?id=hasFooComp":[1689,1868]}})JSON");
  EXPECT_EQ(
      *input[4],
      R"JSON({"interestGroupOwner":"https://fooAds.com","topWindowHostname":"publisher_hostname","adComponents":["adComponent.com/foo_components/id=0","adComponent.com/foo_components/id=1","adComponent.com/foo_components/id=2"],"renderUrl":"fooMeOnceAds.com/render_ad?id=hasFooComp"})JSON");
  EXPECT_EQ(*input[5], "{}");
}

template <typename Request>
void SetupMockCryptoClientWrapper(Request request,
                                  MockCryptoClientWrapper& crypto_client) {
  // Mock the HpkeEncrypt() call on the crypto client.
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
      hpke_encrypt_response;
  hpke_encrypt_response.set_secret(kSecret);
  hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kKeyId);
  hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
      request.SerializeAsString());
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillOnce(testing::Return(hpke_encrypt_response));

  // Mock the HpkeDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
      hpke_decrypt_response;
  hpke_decrypt_response.set_payload(request.SerializeAsString());
  hpke_decrypt_response.set_secret(kSecret);
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly(testing::Return(hpke_decrypt_response));

  // Mock the AeadEncrypt() call on the crypto_client. This is used to encrypt
  // the response coming back from the service.

  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillOnce(
          [](absl::string_view plaintext_payload, absl::string_view secret) {
            google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
            data.set_ciphertext(plaintext_payload);
            google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
                aead_encrypt_response;
            *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
            return aead_encrypt_response;
          });

  // Mock the AeadDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse
      aead_decrypt_response;
  aead_decrypt_response.set_payload(request.SerializeAsString());
  EXPECT_CALL(crypto_client, AeadDecrypt)
      .Times(AnyNumber())
      .WillOnce(testing::Return(aead_decrypt_response));
}

class ScoreAdsReactorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<ScoreAdsRequest>(
        server_common::telemetry::BuildDependentConfig(config_proto))
        ->Get(&request_);
  }
  ScoreAdsResponse ExecuteScoreAds(
      RawRequest& raw_request, MockCodeDispatchClient& dispatcher,
      AuctionServiceRuntimeConfig runtime_config,
      bool enable_report_result_url_generation = false) {
    ScoreAdsResponse response;
    *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
    std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger =
        std::make_unique<ScoreAdsNoOpLogger>();

    // Mock Reporting Client.
    std::unique_ptr<MockAsyncReporter> async_reporter =
        std::make_unique<MockAsyncReporter>(
            std::make_unique<MockHttpFetcherAsync>());
    MockCryptoClientWrapper crypto_client;
    SetupMockCryptoClientWrapper(raw_request, crypto_client);
    TrustedServersConfigClient config_client({});
    config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_client.SetFlagForTest(kTrue, TEST_MODE);
    auto key_fetcher_manager = CreateKeyFetcherManager(
        config_client, /* public_key_fetcher= */ nullptr);
    runtime_config.encryption_enabled = true;
    request_.set_key_id(kKeyId);
    // The last parameter is a flag for whether to parse the Trusted Scoring
    // Signals. It must be set to true for this test to pass.
    ScoreAdsReactor reactor(dispatcher, &request_, &response,
                            std::move(benchmarkingLogger),
                            key_fetcher_manager.get(), &crypto_client,
                            async_reporter.get(), runtime_config);
    reactor.Execute();
    return response;
  }

  ScoreAdsRequest request_;
};

TEST_F(
    ScoreAdsReactorTest,
    CreatesScoreAdInputsWithParsedScoringSignalsForTwoAdsWithSameComponents) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 2);
        if (batch.size() == 2) {
          if (batch.at(0).id == "fooMeOnceAds.com/render_ad?id=hasFooComp") {
            CheckInputCorrectForAdWithFooComp(batch.at(0).input);
            CheckInputCorrectForFoo(batch.at(1).input);
          } else {
            CheckInputCorrectForAdWithFooComp(batch.at(1).input);
            CheckInputCorrectForFoo(batch.at(0).input);
          }
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata foo, has_foo_components;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidSameComponentAsFoo(has_foo_components);
  BuildRawRequest({foo, has_foo_components}, kTestSellerSignals,
                  kTestAuctionSignals, kTestScoringSignals,
                  kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

// See b/258697130.
TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsInCorrectOrderWithParsedScoringSignalsForTwoAds) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 2);
        if (batch.size() == 2) {
          if (batch.at(0).id == "barStandardAds.com/render_ad?id=bar") {
            CheckInputCorrectForFoo(batch.at(1).input);
            CheckInputCorrectForBar(batch.at(0).input);
          } else {
            CheckInputCorrectForFoo(batch.at(0).input);
            CheckInputCorrectForBar(batch.at(1).input);
          }
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

// See b/258697130.
TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsInCorrectOrderWithParsedScoringSignals) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (auto request : batch) {
          auto input = request.input;
          CheckInputCorrectForFoo(input);
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata foo;
  GetTestAdWithBidFoo(foo);
  BuildRawRequest({foo}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsInCorrectOrderWithParsedScoringSignalsForBar) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (auto request : batch) {
          auto input = request.input;
          CheckInputCorrectForBar(input);
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata bar;
  GetTestAdWithBidBar(bar);
  BuildRawRequest({bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesCorrectScoreAdInputsWithParsedScoringSignalsForNoComponentAds) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (auto request : batch) {
          auto input = request.input;
          EXPECT_EQ(*input[0],
                    R"JSON(["brisket","pulled_pork","smoked_chicken"])JSON");
          EXPECT_EQ(*input[1], R"JSON(17.760000)JSON");
          EXPECT_EQ(
              *input[2],
              R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
          EXPECT_EQ(
              *input[3],
              R"JSON({"adComponentRenderUrls":{},"renderUrl":{"barStandardAds.com/render_ad?id=barbecue":[1689,1868]}})JSON");
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisher_hostname","renderUrl":"barStandardAds.com/render_ad?id=barbecue"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata awb;
  GetTestAdWithBidBarbecue(awb);
  BuildRawRequest({awb}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoreAdInputsWellWithParsedScoringSignalsButNotForComponentAds) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (auto request : batch) {
          auto input = request.input;
          EXPECT_EQ(*input[0],
                    R"JSON(["brisket","pulled_pork","smoked_chicken"])JSON");
          EXPECT_EQ(*input[1], R"JSON(17.760000)JSON");
          EXPECT_EQ(
              *input[2],
              R"JSON({"auctionSignals": {"auction_signal": "test 2"}, "sellerSignals": {"seller_signal": "test 1"}})JSON");
          EXPECT_EQ(
              *input[3],
              R"JSON({"adComponentRenderUrls":{},"renderUrl":{"barStandardAds.com/render_ad?id=barbecue2":[1689,1868]}})JSON");
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisher_hostname","adComponents":["barStandardAds.com/ad_components/id=0"],"renderUrl":"barStandardAds.com/render_ad?id=barbecue2"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata awb;
  GetTestAdWithBidBarbecueWithComponents(awb);
  BuildRawRequest({awb}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest, CreatesScoringSignalInputPerAdWithSignal) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  std::vector<AdWithBidMetadata> ads;
  AdWithBidMetadata no_signal_ad = MakeARandomAdWithBidMetadata(2, 2);
  no_signal_ad.set_render("no_signal");
  ads.push_back(no_signal_ad);
  std::string trusted_scoring_signals =
      R"json({"renderUrls":{"placeholder_url":[123])json";
  absl::flat_hash_set<std::string> scoring_signal_per_ad;
  for (int i = 0; i < 100; i++) {
    const AdWithBidMetadata ad = MakeARandomAdWithBidMetadata(2, 2);
    ads.push_back(ad);
    std::string ad_signal =
        absl::StrFormat("\"%s\":%s", ad.render(), R"JSON(123)JSON");

    scoring_signal_per_ad.emplace(absl::StrFormat(
        "{\"adComponentRenderUrls\":{},\"renderUrl\":{%s}}", ad_signal));
    absl::StrAppend(&trusted_scoring_signals,
                    absl::StrFormat(", %s", ad_signal));
  }
  absl::StrAppend(&trusted_scoring_signals,
                  R"json(},"adComponentRenderUrls":{}})json");

  EXPECT_EQ(ads[0].render(), no_signal_ad.render());
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([scoring_signal_per_ad, no_signal_ad](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback done_callback) {
        for (int i = 0; i < batch.size(); i++) {
          auto& request = batch.at(i);
          auto input = request.input;
          // An ad with no signal should never make it into the execution batch.
          EXPECT_NE(no_signal_ad.render(), batch.at(i).id);
          EXPECT_TRUE(scoring_signal_per_ad.contains(*input[3]));
        }
        return absl::OkStatus();
      });

  BuildRawRequest(ads, kTestSellerSignals, kTestAuctionSignals,
                  trusted_scoring_signals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest, EmptySignalsResultsInNoResponse) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  AdWithBidMetadata foo;
  GetTestAdWithBidFoo(foo);
  BuildRawRequest({foo}, "", "", "", "", raw_request);
  const ScoreAdsResponse response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoresForAllAdsRequestedWithoutComponentAuction) {
  MockCodeDispatchClient dispatcher;
  int current_score = 1;
  bool allowComponentAuction = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  RawRequest raw_request_copy = raw_request;

  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (auto ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        ABSL_LOG(INFO) << "Batch executing";
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (auto request : batch) {
          ABSL_LOG(INFO) << "Accessing id ad mapping for " << request.id;
          ++current_score;
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          ABSL_LOG(INFO) << "Successfully accessed id ad mapping for "
                         << request.id;
          score_logic.push_back(absl::Substitute(
              R"(
      {
      "response" : {
        "desirability" : $0,
        "allowComponentAuction" : $1
      },
      "logs":[]
      }
)",
              current_score, (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic));
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  ABSL_LOG(INFO) << "Accessing score to ad mapping for "
                 << scored_ad.desirability();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  ABSL_LOG(INFO) << "Accessed score to ad mapping for "
                 << scored_ad.desirability();
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(),
            original_ad_with_bid.interest_group_owner());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming not component auctions, verify.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map().size(), 1);
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
  ABSL_LOG(INFO)
      << "Accessing mapping for ig_owner_highest_scoring_other_bids_map";
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map()
                .at(kInterestGroupOwnerOfBarBidder)
                .values()
                .at(0)
                .number_value(),
            2);
  ABSL_LOG(INFO)
      << "Accessed mapping for ig_owner_highest_scoring_other_bids_map";
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoresForAllAdsRequestedWithHighestOtherBids) {
  MockCodeDispatchClient dispatcher;
  int current_score = 1;
  bool allowComponentAuction = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  RawRequest raw_request_copy = raw_request;

  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (auto ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (auto request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              R"(
      {
      "response" : {
        "desirability" : $0,
        "allowComponentAuction" : $1
      },
      "logs":[]
      }
)",
              current_score, (allowComponentAuction) ? "true" : "false"));
          current_score++;
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.interest_group_owner(),
            original_ad_with_bid.interest_group_owner());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming not component auctions, verify.
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map().size(), 1);
  EXPECT_EQ(scored_ad.ig_owner_highest_scoring_other_bids_map()
                .at(kInterestGroupOwnerOfBarBidder)
                .values()
                .at(0)
                .number_value(),
            2);
}

TEST_F(ScoreAdsReactorTest, PassesTopLevelSellerToComponentAuction) {
  MockCodeDispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        for (const auto& request : batch) {
          const auto& input = request.input;
          EXPECT_EQ(
              *input[4],
              R"JSON({"interestGroupOwner":"barStandardAds.com","topWindowHostname":"publisher_hostname","adComponents":["barStandardAds.com/ad_components/id=0"],"topLevelSeller":"testTopLevelSeller","renderUrl":"barStandardAds.com/render_ad?id=barbecue2"})JSON");
        }
        return absl::OkStatus();
      });
  RawRequest raw_request;
  AdWithBidMetadata awb;
  GetTestAdWithBidBarbecueWithComponents(awb);
  BuildRawRequestForComponentAuction({awb}, kTestSellerSignals,
                                     kTestAuctionSignals, kTestScoringSignals,
                                     kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());
}

TEST_F(ScoreAdsReactorTest,
       CreatesScoresForAllAdsRequestedWithComponentAuction) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequestForComponentAuction({foo, bar}, kTestSellerSignals,
                                     kTestAuctionSignals, kTestScoringSignals,
                                     kTestPublisherHostname, raw_request);
  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (auto ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (auto request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(absl::Substitute(
              R"(
      {
      "response" : {
        "ad": "adMetadata",
        "desirability" : $0,
        "bid" : $1,
        "allowComponentAuction" : $2
      },
      "logs":[]
      }
)",
              current_score++, 1 + (std::rand() % 20),
              (allowComponentAuction) ? "true" : "false"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  auto original_ad_with_bid = score_to_ad.at(scored_ad.desirability());
  // All scored ads must have these next four fields present and set.
  // The next three of these are all taken from the ad_with_bid.
  EXPECT_EQ(scored_ad.render(), original_ad_with_bid.render());
  EXPECT_EQ(scored_ad.component_renders_size(), 3);
  EXPECT_EQ(scored_ad.component_renders().size(),
            original_ad_with_bid.ad_components().size());
  EXPECT_EQ(scored_ad.interest_group_name(),
            original_ad_with_bid.interest_group_name());
  EXPECT_EQ(scored_ad.buyer_bid(), original_ad_with_bid.bid());
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  // Since in the above test we are assuming component auctions, check that the
  // required fields for component auctions are set.
  EXPECT_TRUE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_metadata(), "adMetadata");
  EXPECT_GT(scored_ad.bid(), std::numeric_limits<float>::min());
}

TEST_F(ScoreAdsReactorTest, ParsesJsonAdMetadataInComponentAuction) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequestForComponentAuction({foo, bar}, kTestSellerSignals,
                                     kTestAuctionSignals, kTestScoringSignals,
                                     kTestPublisherHostname, raw_request);
  RawRequest raw_request_copy = raw_request;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_logic.push_back(
              R"(
      {
      "response" : {
        "ad": {"key1":"adMetadata"},
        "desirability" : 0.1,
        "bid" : 0.1,
        "allowComponentAuction" : true
      },
      "logs":[]
      }
)");
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });
  auto response =
      ExecuteScoreAds(raw_request, dispatcher, AuctionServiceRuntimeConfig());

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()))
      << "ScoreAdsResponse could not be parsed: "
      << response.response_ciphertext();
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.ad_metadata(), "{\"key1\":\"adMetadata\"}");
}

TEST_F(ScoreAdsReactorTest, CreatesDebugUrlsForAllAds) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  true);

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (auto request : batch) {
          score_logic.push_back(absl::StrCat(
              "{\"response\":"
              "{\"desirability\": ",
              current_score++, ", \"bid\": ", 1 + (std::rand() % 20),
              ", \"allowComponentAuction\": ",
              ((allowComponentAuction) ? "true" : "false"),
              ", \"debugReportUrls\": {",
              "    \"auctionDebugLossUrl\" : "
              "\"https://example-ssp.com/debugLoss\",",
              "    \"auctionDebugWinUrl\" : "
              "\"https://example-ssp.com/debugWin\"",
              "}}, \"logs\":[]}"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true, true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_TRUE(scored_ad.has_debug_report_urls());
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_loss_url(),
            "https://example-ssp.com/debugLoss");
  EXPECT_EQ(scored_ad.debug_report_urls().auction_debug_win_url(),
            "https://example-ssp.com/debugWin");
}

TEST_F(ScoreAdsReactorTest, SuccessExecutesInRomaWithLogsEnabled) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  true);
  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad](std::vector<DispatchRequest>& batch,
                                  BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_to_ad.insert_or_assign(current_score, id_to_ad.at(request.id));
          score_logic.push_back(
              absl::StrCat("{\"response\":{\"desirability\": ", current_score++,
                           ", \"bid\": ", 1 + (std::rand() % 20),
                           ", \"allowComponentAuction\": ",
                           ((allowComponentAuction) ? "true" : "false"),
                           ", \"debugReportUrls\": {",
                           "    \"auctionDebugLossUrl\" : "
                           "\"https://example-ssp.com/debugLoss\",",
                           "    \"auctionDebugWinUrl\" : "
                           "\"https://example-ssp.com/debugWin\"",
                           "}},"
                           "\"logs\":[\"test log\"],"
                           "\"errors\":[\"test error\"],"
                           "\"warnings\":[\"test warning\"]"
                           "}"));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true, true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true,
      .enable_adtech_code_logging = true};
  const auto& response =
      ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
}

TEST_F(ScoreAdsReactorTest, SuccessfullyExecutesReportResult) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = false;
  bool enable_report_result_url_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  enable_debug_reporting);
  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            response.emplace_back(kTestReportingResponseJson);
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation};
  const auto& response = ExecuteScoreAds(
      raw_request, dispatcher, runtime_config, enable_debug_reporting);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

TEST_F(ScoreAdsReactorTest,
       SuccessfullyExecutesReportResultAndReportWinForComponentAuctions) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = true;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  enable_debug_reporting, enable_adtech_code_logging,
                  kTopLevelSeller);
  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request_copy.ad_bids()) {
    // Make sure id(render id) is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting,
                       enable_report_result_win_generation](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            if (enable_report_result_win_generation) {
              response.emplace_back(kTestReportingWinResponseJson);
            } else {
              response.emplace_back(kTestReportingResponseJson);
            }
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation};
  const auto& response = ExecuteScoreAds(
      raw_request, dispatcher, runtime_config, enable_debug_reporting);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

TEST_F(ScoreAdsReactorTest, SuccessfullyExecutesReportResultAndReportWin) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  enable_debug_reporting, enable_adtech_code_logging);
  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting,
                       enable_report_result_win_generation](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            if (enable_report_result_win_generation) {
              response.emplace_back(kTestReportingWinResponseJson);
            } else {
              response.emplace_back(kTestReportingResponseJson);
            }
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation};
  const auto& response = ExecuteScoreAds(
      raw_request, dispatcher, runtime_config, enable_debug_reporting);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

TEST_F(ScoreAdsReactorTest, ReportResultFailsReturnsOkayResponse) {
  MockCodeDispatchClient dispatcher;
  int current_score = 0;
  bool allowComponentAuction = false;
  bool enable_adtech_code_logging = false;
  bool enable_report_result_url_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);
  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  enable_debug_reporting);
  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, AdWithBidMetadata> id_to_ad;
  for (const auto& ad : raw_request_copy.ad_bids()) {
    // Make sure id is tracked to render urls to stay unique.
    id_to_ad.insert_or_assign(ad.render(), ad);
  }
  absl::flat_hash_map<double, AdWithBidMetadata> score_to_ad;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&current_score, &allowComponentAuction, &score_to_ad,
                       &id_to_ad, enable_adtech_code_logging,
                       enable_debug_reporting](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> response;
        for (const auto& request : batch) {
          if (std::strcmp(request.handler_name.c_str(),
                          kReportingDispatchHandlerFunctionName) == 0) {
            std::vector<absl::StatusOr<DispatchResponse>> responses;
            done_callback(responses);
            return absl::ErrnoToStatus(1, "Unknown error");
          } else {
            score_to_ad.insert_or_assign(current_score,
                                         id_to_ad.at(request.id));
            response.push_back(absl::StrFormat(
                R"JSON(
              {
              "response": {
                "desirability":%d,
                "bid":%f,
                "allowComponentAuction":%s
              },
              "logs":["test log"]
              }
              )JSON",
                current_score++, 1 + (std::rand() % 20),
                ((allowComponentAuction) ? "true" : "false")));
          }
        }
        return FakeExecute(batch, std::move(done_callback), std::move(response),
                           enable_debug_reporting, enable_adtech_code_logging);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation};
  const auto& response = ExecuteScoreAds(
      raw_request, dispatcher, runtime_config, enable_debug_reporting);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            "");
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            0);
}

TEST_F(ScoreAdsReactorTest, IgnoresUnknownFieldsFromScoreAdResponse) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  AdWithBidMetadata foo;
  GetTestAdWithBidFoo(foo);
  BuildRawRequest({foo}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  true);
  RawRequest raw_request_copy = raw_request;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.push_back(absl::Substitute(R"(
      {
      "response" : {
        "desirability" : 1,
        "bid" : $0,
        "unknownFieldKey" : 0
      },
      "logs":[]
      }
)",
                                               1 + (std::rand() % 20)));
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Ad was parsed successfully.
  EXPECT_EQ(scored_ad.desirability(), 1);
}

TEST_F(ScoreAdsReactorTest, VerifyDecryptionEncryptionSuccessful) {
  MockCodeDispatchClient dispatcher;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.push_back(absl::Substitute(R"(
      {
      "response" : {
        "desirability" : 1,
        "bid" : $0,
        "unknownFieldKey" : 0
      },
      "logs":[]
      }
)",
                                               1 + (std::rand() % 20)));
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), false);
      });

  RawRequest raw_request;
  AdWithBidMetadata foo;
  GetTestAdWithBidFoo(foo);
  BuildRawRequest({foo}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);

  ScoreAdsResponse response;
  request_.set_key_id("key_id");
  request_.set_request_ciphertext("ciphertext");
  std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger =
      std::make_unique<ScoreAdsNoOpLogger>();

  // Return an empty key.
  server_common::MockKeyFetcherManager key_fetcher_manager;
  server_common::PrivateKey private_key;
  EXPECT_CALL(key_fetcher_manager, GetPrivateKey)
      .WillOnce(testing::Return(private_key));

  MockCryptoClientWrapper crypto_client;
  // Mock the HpkeDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse decrypt_response;
  decrypt_response.set_payload(raw_request.SerializeAsString());
  decrypt_response.set_secret("secret");
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .WillOnce(testing::Return(decrypt_response));

  // Mock the AeadEncrypt() call on the crypto_client. This is used for
  // encrypting the response.
  google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
  data.set_ciphertext("ciphertext");
  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse encrypt_response;
  *encrypt_response.mutable_encrypted_data() = std::move(data);
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .WillOnce(testing::Return(encrypt_response));

  // Mock Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  AuctionServiceRuntimeConfig runtime_config = {.encryption_enabled = true};
  ScoreAdsReactor reactor(dispatcher, &request_, &response,
                          std::move(benchmarkingLogger), &key_fetcher_manager,
                          &crypto_client, async_reporter.get(), runtime_config);
  reactor.Execute();

  EXPECT_FALSE(response.response_ciphertext().empty());
}

// This test case validates that reject reason is returned in response of
// ScoreAds for ads where it was of any valid value other than 'not-available'.
TEST_F(ScoreAdsReactorTest, CaptureRejectionReasonsForRejectedAds) {
  MockCodeDispatchClient dispatcher;
  bool allowComponentAuction = false;
  RawRequest raw_request;
  AdWithBidMetadata foo, bar;
  GetTestAdWithBidFoo(foo);
  GetTestAdWithBidBar(bar);

  BuildRawRequest({foo, bar}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  true);

  RawRequest raw_request_copy = raw_request;
  absl::flat_hash_map<std::string, std::string> id_to_rejection_reason;
  // Make sure id is tracked to render urls to stay unique.
  id_to_rejection_reason.insert_or_assign(bar.render(), "not-available");
  id_to_rejection_reason.insert_or_assign(foo.render(), "invalid-bid");

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([&allowComponentAuction, &id_to_rejection_reason](
                          std::vector<DispatchRequest>& batch,
                          BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (auto request : batch) {
          std::string rejection_reason = id_to_rejection_reason.at(request.id);
          score_logic.push_back(absl::Substitute(
              R"(
      {
      "response" : {
        "desirability" : $0,
        "bid" : $1,
        "allowComponentAuction" : $2,
        "rejectReason" : "$3"
      },
      "logs":[]
      }
)",
              1, 1 + (std::rand() % 20),
              (allowComponentAuction) ? "true" : "false", rejection_reason));
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Desirability must be present but was determined by the scoring code.
  EXPECT_GT(scored_ad.desirability(), std::numeric_limits<float>::min());
  EXPECT_EQ(scored_ad.ad_rejection_reasons().size(), 1);
  const auto& ad_rejection_reason = scored_ad.ad_rejection_reasons().at(0);
  EXPECT_EQ(ad_rejection_reason.interest_group_name(),
            foo.interest_group_name());
  EXPECT_EQ(ad_rejection_reason.interest_group_owner(),
            foo.interest_group_owner());
  EXPECT_EQ(ad_rejection_reason.rejection_reason(),
            SellerRejectionReason::INVALID_BID);
}

TEST_F(ScoreAdsReactorTest,
       ScoredAdRemovedFromConsiderationWhenRejectReasonAvailable) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  AdWithBidMetadata ad;
  GetTestAdWithBidFoo(ad);

  BuildRawRequest({ad}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_logic.push_back(
              R"(
                  {
                  "response" : {
                    "desirability" : 1000,
                    "bid" : 1000,
                    "allowComponentAuction" : false,
                    "rejectReason" : "invalid-bid"
                  },
                  "logs":[]
                  })");
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));

  // No ad is present in the response even though the scored ad had non-zero
  // desirability.
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest, ZeroDesirabilityAdsConsideredAsRejected) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  AdWithBidMetadata ad;
  GetTestAdWithBidFoo(ad);

  BuildRawRequest({ad}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request);
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillRepeatedly([](std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback done_callback) {
        // Each original ad request (AdWithBidMetadata) is stored by its
        // expected score and later compared to the output AdScore with the
        // matching score.
        std::vector<std::string> score_logic;
        for (const auto& request : batch) {
          score_logic.push_back(R"(
          {
            "response" : 0
          })");
        }
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  ASSERT_TRUE(raw_response.ParseFromString(response.response_ciphertext()));

  // No ad is present in the response when a zero desirability is returned
  // for the scored ad.
  EXPECT_FALSE(raw_response.has_ad_score());
}

TEST_F(ScoreAdsReactorTest, SingleNumberResponseFromScoreAdIsValid) {
  MockCodeDispatchClient dispatcher;
  RawRequest raw_request;
  AdWithBidMetadata foo;
  GetTestAdWithBidFoo(foo);
  BuildRawRequest({foo}, kTestSellerSignals, kTestAuctionSignals,
                  kTestScoringSignals, kTestPublisherHostname, raw_request,
                  true);
  RawRequest raw_request_copy = raw_request;
  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        std::vector<std::string> score_logic;
        score_logic.push_back(absl::Substitute(R"(
      {
      "response" : 1.5
      }
)"));
        return FakeExecute(batch, std::move(done_callback),
                           std::move(score_logic), true);
      });
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true};
  auto response = ExecuteScoreAds(raw_request, dispatcher, runtime_config);

  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  auto scored_ad = raw_response.ad_score();
  // Ad was parsed successfully.
  EXPECT_DOUBLE_EQ(scored_ad.desirability(), 1.5);
  EXPECT_FALSE(scored_ad.allow_component_auction());
}

void BuildProtectedAppSignalsRawRequest(
    const std::vector<ProtectedAppSignalsAdWithBidMetadata>&
        ads_with_bids_to_add,
    const std::string& seller_signals, const std::string& auction_signals,
    const std::string& scoring_signals, const std::string& publisher_hostname,
    RawRequest& output, bool enable_debug_reporting = false) {
  for (int i = 0; i < ads_with_bids_to_add.size(); i++) {
    output.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids_to_add[i].owner(), kTestBuyerSignals);
    *output.add_protected_app_signals_ad_bids() =
        std::move(ads_with_bids_to_add[i]);
  }
  output.set_seller_signals(seller_signals);
  output.set_auction_signals(auction_signals);
  output.set_scoring_signals(scoring_signals);
  output.set_publisher_hostname(publisher_hostname);
  output.set_enable_debug_reporting(enable_debug_reporting);
}

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid = kTestBid) {
  ProtectedAppSignalsAdWithBidMetadata ad;
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(render_url, "arbitraryMetadataKey", 2));
  ad.set_render(render_url);
  ad.set_bid(bid);
  ad.set_owner(kTestProtectedAppSignalsAdOwner);
  ad.set_egress_features(kTestEgressFeatures);
  return ad;
}

class ScoreAdsReactorProtectedAppSignalsTest : public ScoreAdsReactorTest {
 protected:
  void SetUp() override {
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<ScoreAdsRequest>(
        server_common::telemetry::BuildDependentConfig(config_proto))
        ->Get(&request_);

    config_client_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_client_.SetFlagForTest(kTrue, TEST_MODE);

    runtime_config_.encryption_enabled = true;
    runtime_config_.enable_protected_app_signals = true;

    key_fetcher_manager_ =
        CreateKeyFetcherManager(config_client_, /*public_key_fetcher=*/nullptr);
  }

  ScoreAdsResponse ExecuteScoreAds(const RawRequest& raw_request,
                                   MockCodeDispatchClient& dispatcher) {
    SetupMockCryptoClientWrapper(raw_request, crypto_client_);
    *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
    request_.set_key_id(kKeyId);

    ScoreAdsResponse response;
    ScoreAdsReactor reactor(dispatcher, &request_, &response,
                            std::move(benchmarkingLogger_),
                            key_fetcher_manager_.get(), &crypto_client_,
                            async_reporter_.get(), runtime_config_);
    reactor.Execute();
    return response;
  }

  ScoreAdsRequest request_;
  AuctionServiceRuntimeConfig runtime_config_;
  TrustedServersConfigClient config_client_{{}};
  std::unique_ptr<MockAsyncReporter> async_reporter_ =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  MockCryptoClientWrapper crypto_client_;
  std::unique_ptr<ScoreAdsBenchmarkingLogger> benchmarkingLogger_ =
      std::make_unique<ScoreAdsNoOpLogger>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
};

TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       AdDispatchedForScoringWhenSignalsPresent) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute)
      .WillOnce([](std::vector<DispatchRequest>& batch,
                   BatchDispatchDoneCallback done_callback) {
        EXPECT_EQ(batch.size(), 1);
        const auto& req = batch[0];
        EXPECT_EQ(req.handler_name, "scoreAdEntryFunction");

        EXPECT_EQ(req.input.size(), 7);
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kAdMetadata)],
                  "{\"arbitraryMetadataKey\":2}");
        EXPECT_THAT(*req.input[static_cast<int>(ScoreAdArgs::kBid)],
                    HasSubstr(absl::StrCat(kTestBid)));
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kAuctionConfig)],
                  "{\"auctionSignals\": {\"auction_signal\": \"test 2\"}, "
                  "\"sellerSignals\": {\"seller_signal\": \"test 1\"}}");
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kScoringSignals)],
                  "{\"renderUrl\":{\"testAppAds.com/"
                  "render_ad?id=bar\":[\"test_signal\"]}}");
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kDeviceSignals)],
                  "\"\"");
        EXPECT_EQ(
            *req.input[static_cast<int>(ScoreAdArgs::kDirectFromSellerSignals)],
            "{}");
        EXPECT_EQ(*req.input[static_cast<int>(ScoreAdArgs::kFeatureFlags)],
                  "{\"enable_logging\": false,\"enable_debug_url_generation\": "
                  "false}");
        return absl::OkStatus();
      });
  RawRequest raw_request;
  ProtectedAppSignalsAdWithBidMetadata ad =
      GetProtectedAppSignalsAdWithBidMetadata(kTestProtectedAppSignalsRenderUrl,
                                              kTestBid);
  BuildProtectedAppSignalsRawRequest(
      {std::move(ad)}, kTestSellerSignals, kTestAuctionSignals,
      kTestProtectedAppScoringSignals, kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher);
}

TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       AdNotDispatchedForScoringWhenSignalsAbsent) {
  MockCodeDispatchClient dispatcher;

  EXPECT_CALL(dispatcher, BatchExecute).Times(0);
  RawRequest raw_request;
  ProtectedAppSignalsAdWithBidMetadata ad =
      GetProtectedAppSignalsAdWithBidMetadata(
          kTestProtectedAppSignalsRenderUrl);
  BuildProtectedAppSignalsRawRequest(
      {std::move(ad)}, kTestSellerSignals, kTestAuctionSignals,
      /*scoring_signals=*/R"JSON({"renderUrls": {}})JSON",
      kTestPublisherHostname, raw_request);
  ExecuteScoreAds(raw_request, dispatcher);
}

TEST_F(ScoreAdsReactorProtectedAppSignalsTest,
       SuccessfullyExecutesReportResultAndReportWin) {
  MockCodeDispatchClient dispatcher;
  bool enable_adtech_code_logging = true;
  bool enable_report_result_url_generation = true;
  bool enable_report_result_win_generation = true;
  bool enable_debug_reporting = false;
  RawRequest raw_request;
  ProtectedAppSignalsAdWithBidMetadata protected_app_signals_ad_with_bid =
      GetProtectedAppSignalsAdWithBidMetadata(
          kTestProtectedAppSignalsRenderUrl);
  BuildProtectedAppSignalsRawRequest(
      {std::move(protected_app_signals_ad_with_bid)}, kTestSellerSignals,
      kTestAuctionSignals, kTestProtectedAppScoringSignals,
      kTestPublisherHostname, raw_request, enable_debug_reporting);
  {
    InSequence s;
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillOnce([](std::vector<DispatchRequest>& batch,
                     BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          PS_VLOG(0) << "UDF handler: " << request.handler_name;
          EXPECT_EQ(request.handler_name, "scoreAdEntryFunction");
          auto incoming_bid =
              request.input[static_cast<int>(ScoreAdArgs::kBid)];
          float bid;
          EXPECT_TRUE(absl::SimpleAtof(*incoming_bid, &bid))
              << "Failed to convert bid to float: " << *incoming_bid;
          std::vector<absl::StatusOr<DispatchResponse>> responses;
          DispatchResponse response = {.id = request.id,
                                       .resp = absl::StrFormat(
                                           R"JSON(
                  {
                    "response": {
                      "desirability":%d,
                      "bid":%f
                    },
                    "logs":["test log"]
                    }
                  )JSON",
                                           kTestDesirability, bid)};
          done_callback({response});
          return absl::OkStatus();
        });
    EXPECT_CALL(dispatcher, BatchExecute)
        .WillRepeatedly([enable_report_result_win_generation](
                            std::vector<DispatchRequest>& batch,
                            BatchDispatchDoneCallback done_callback) {
          EXPECT_EQ(batch.size(), 1);
          const auto& request = batch[0];
          PS_VLOG(0) << "UDF handler: " << request.handler_name;
          EXPECT_EQ(request.handler_name,
                    kReportingProtectedAppSignalsFunctionName);
          EXPECT_EQ(
              *request.input[ReportingArgIndex(ReportingArgs::kEgressFeatures)],
              kTestEgressFeatures);
          DispatchResponse response;
          if (enable_report_result_win_generation) {
            response.resp = kTestReportingWinResponseJson;
          } else {
            response.resp = kTestReportingResponseJson;
          }
          response.id = request.id;
          done_callback({response});
          return absl::OkStatus();
        });
  }
  runtime_config_ = {
      .encryption_enabled = true,
      .enable_seller_debug_url_generation = enable_debug_reporting,
      .enable_adtech_code_logging = enable_adtech_code_logging,
      .enable_report_result_url_generation =
          enable_report_result_url_generation,
      .enable_report_win_url_generation = enable_report_result_win_generation,
      .enable_protected_app_signals = true};
  const auto& response = ExecuteScoreAds(raw_request, dispatcher);
  ScoreAdsResponse::ScoreAdsRawResponse raw_response;
  raw_response.ParseFromString(response.response_ciphertext());
  const auto& scored_ad = raw_response.ad_score();
  EXPECT_EQ(scored_ad.desirability(), kTestDesirability);
  EXPECT_EQ(scored_ad.render(), kTestProtectedAppSignalsRenderUrl);
  EXPECT_EQ(scored_ad.component_renders().size(), 0);
  EXPECT_EQ(scored_ad.interest_group_name(), "");
  EXPECT_EQ(scored_ad.interest_group_owner(), kTestProtectedAppSignalsAdOwner);
  EXPECT_EQ(scored_ad.buyer_bid(), kTestBid);
  EXPECT_FALSE(scored_ad.allow_component_auction());
  EXPECT_EQ(scored_ad.ad_type(), AdType::AD_TYPE_PROTECTED_APP_SIGNALS_AD);

  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
  EXPECT_EQ(
      scored_ad.win_reporting_urls().buyer_reporting_urls().reporting_url(),
      kTestReportWinUrl);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .size(),
            1);
  EXPECT_EQ(scored_ad.win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestInteractionEvent),
            kTestInteractionUrl);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
