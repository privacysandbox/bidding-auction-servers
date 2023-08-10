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

#include "services/bidding_service/generate_bids_reactor.h"

#include <memory>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
constexpr char testAuctionSignals[] = R"json({"auction_signal": "test 1"})json";
constexpr char testBuyerSignals[] = R"json({"buyer_signal": "test 2"})json";
// Bidding signals must be contained in "keys" in root object.
constexpr char testBiddingSignals[] =
    R"json({"keys":{"bidding_signal": "test 3"}})json";
constexpr char kSeller[] = "https://www.example-ssp.com";
constexpr char kPublisherName[] = "www.example-publisher.com";
constexpr char kKeyId[] = "key_id";
constexpr char kSecret[] = "secret";
constexpr char kTestRenderUrl[] = "test.com";

using ::google::protobuf::TextFormat;

using Request = GenerateBidsRequest;
using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using Response = GenerateBidsResponse;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;
using ::testing::AnyNumber;

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

absl::Status FakeExecute(std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback batch_callback,
                         const std::string& json) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;
  for (const auto& request : batch) {
    EXPECT_EQ(request.handler_name, "generateBidEntryFunction");
    DispatchResponse dispatch_response = {};
    dispatch_response.resp = json;
    dispatch_response.id = request.id;
    responses.emplace_back(dispatch_response);
  }
  batch_callback(responses);
  return absl::OkStatus();
}

std::string GetTestResponse(const std::string& render, int bid) {
  return absl::Substitute(R"JSON({
    "response": {
      "render": "$0",
      "bid": $1
    },
    "logs": ["test log"],
    "errors": ["test.error"],
    "warnings":["test.warn"]
  })JSON",
                          render, bid);
}

// TODO(b/257649113): Incorporate new fields in InterestGroupForBidding.
class GenerateBidsReactorTest : public testing::Test {
 public:
  MockCodeDispatchClient dispatcher_;

 protected:
  void SetUp() override {
    // initialize
    server_common::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::TelemetryConfig::PROD);
    metric::BiddingContextMap(server_common::BuildDependentConfig(config_proto))
        ->Get(&request_);

    TrustedServersConfigClient config_client({});
    config_client.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    config_client.SetFlagForTest(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(config_client);
    SetupMockCryptoClientWrapper(*crypto_client_);
    request_.set_key_id(kKeyId);
    auto raw_request = MakeARandomGenerateBidsRawRequestForAndroid();
    request_.set_request_ciphertext(raw_request.SerializeAsString());
  }

  void CheckGenerateBids(const RawRequest& raw_request,
                         Response expected_response,
                         bool enable_buyer_debug_url_generation = false,
                         bool enable_adtech_code_logging = false) {
    Response response;
    std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
        std::make_unique<BiddingNoOpLogger>();
    BiddingServiceRuntimeConfig runtime_config = {
        .encryption_enabled = true,
        .enable_buyer_debug_url_generation = enable_buyer_debug_url_generation,
        .enable_adtech_code_logging = enable_adtech_code_logging};
    request_.set_request_ciphertext(raw_request.SerializeAsString());
    GenerateBidsReactor reactor(
        dispatcher_, &request_, &response, std::move(benchmarkingLogger),
        key_fetcher_manager_.get(), crypto_client_.get(),
        std::move(runtime_config));
    reactor.Execute();
    google::protobuf::util::MessageDifferencer diff;
    std::string diff_output;
    diff.ReportDifferencesToString(&diff_output);
    GenerateBidsResponse::GenerateBidsRawResponse raw_response;
    raw_response.ParseFromString(response.response_ciphertext());
    diff.TreatAsSet(raw_response.GetDescriptor()->FindFieldByName("bids"));
    GenerateBidsResponse::GenerateBidsRawResponse expected_raw_response;
    expected_raw_response.ParseFromString(
        expected_response.response_ciphertext());
    EXPECT_TRUE(diff.Compare(expected_raw_response, raw_response));
    EXPECT_EQ(diff_output, "");
  }

  Request request_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
};

constexpr char kUserBiddingSignals[] =
    R"JSON({"userBiddingSignalKey": 123})JSON";

IGForBidding GetIGForBiddingFoo() {
  IGForBidding interest_group;
  interest_group.set_name("Foo");
  interest_group.set_user_bidding_signals(kUserBiddingSignals);
  interest_group.mutable_trusted_bidding_signals_keys()->Add("bidding_signal");

  interest_group.mutable_ad_render_ids()->Add("1689");
  interest_group.mutable_ad_render_ids()->Add("1776");

  BrowserSignals browser_signals;
  interest_group.mutable_browser_signals()->set_join_count(5);
  interest_group.mutable_browser_signals()->set_bid_count(25);
  interest_group.mutable_browser_signals()->set_recency(1684134092);
  interest_group.mutable_browser_signals()->set_prev_wins(
      MakeRandomPreviousWins(interest_group.ad_render_ids(), true));
  return interest_group;
}

constexpr char foo_browser_signals[] =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","topLevelSeller":"https://www.example-ssp.com","joinCount":5,"bidCount":25,"recency":1684134092,"prevWins":[[1,"1689"],[1,"1776"]]})json";

IGForBidding GetIGForBiddingBar(bool make_browser_signals = true) {
  IGForBidding interest_group;
  interest_group.set_name("Bar");
  interest_group.set_user_bidding_signals(kUserBiddingSignals);
  interest_group.mutable_trusted_bidding_signals_keys()->Add("bidding_signal");
  interest_group.mutable_ad_render_ids()->Add("1868");
  interest_group.mutable_ad_render_ids()->Add("1954");

  if (make_browser_signals) {
    interest_group.mutable_browser_signals()->set_join_count(5);
    interest_group.mutable_browser_signals()->set_bid_count(25);
    interest_group.mutable_browser_signals()->set_recency(1684134093);
    interest_group.mutable_browser_signals()->set_prev_wins(
        MakeRandomPreviousWins(interest_group.ad_render_ids(), true));
  }
  return interest_group;
}

constexpr char bar_browser_signals[] =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","topLevelSeller":"https://www.example-ssp.com","joinCount":5,"bidCount":25,"recency":1684134093,"prevWins":[[1,"1868"],[1,"1954"]]})json";

// Allows re-serialization.
void CheckForAndReplaceUBSWithEmptyString(
    std::string& serialized_ig, absl::string_view user_bidding_signals) {
  // Check for the presence of the correct user bidding signals
  auto index_of_ubs = serialized_ig.find(user_bidding_signals);
  EXPECT_NE(index_of_ubs, std::string::npos);
  // UBS will not deserialize into a string (hence the custom serialization
  // logic, so we excise it from the string before going back to a message.
  VLOG(5) << "\nDebugging test: Before:\n" << serialized_ig;
  serialized_ig.replace(index_of_ubs, user_bidding_signals.length(),
                        R"JSON("")JSON");
  VLOG(5) << "\nDebugging test: After:\n" << serialized_ig;
}

void CheckCorrectnessOfBar(std::string& serialized_actual_bar) {
  CheckForAndReplaceUBSWithEmptyString(serialized_actual_bar,
                                       kUserBiddingSignals);
  IGForBidding reconstituted_actual_bar;
  // Re-create a Message to run the rest of the checking on (since fields may be
  // serialized in non-deterministic orders).
  google::protobuf::util::JsonStringToMessage(serialized_actual_bar,
                                              &reconstituted_actual_bar);
  reconstituted_actual_bar.clear_user_bidding_signals();
  // Make the expected IG as well to compare to.
  auto expected_bar = GetIGForBiddingBar();
  // Expected Bar needs device signals cleared since they will not be in the
  // actual bar.
  expected_bar.clear_DeviceSignals();
  // Since UBS will not be equal after re-serialization, clear those as well in
  // both.
  expected_bar.clear_user_bidding_signals();
  bool match = google::protobuf::util::MessageDifferencer::Equals(
      expected_bar, reconstituted_actual_bar);
  EXPECT_TRUE(match);
  if (!match) {
    std::string expected_bar_as_str, actual_for_comparison_as_str;
    google::protobuf::util::MessageToJsonString(expected_bar,
                                                &expected_bar_as_str);
    google::protobuf::util::MessageToJsonString(reconstituted_actual_bar,
                                                &actual_for_comparison_as_str);
    VLOG(0) << "\nExpected:\n"
            << expected_bar_as_str << "\nActual:\n"
            << actual_for_comparison_as_str;
  }
}

void BuildRawRequest(const std::vector<IGForBidding>& interest_groups_to_add,
                     const std::string& auction_signals,
                     const std::string& buyer_signals,
                     const std::string& bidding_signals,
                     RawRequest& raw_request,
                     bool enable_debug_reporting = false) {
  for (int i = 0; i < interest_groups_to_add.size(); i++) {
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        interest_groups_to_add[i];
  }
  raw_request.set_auction_signals(auction_signals);
  raw_request.set_buyer_signals(buyer_signals);
  raw_request.set_bidding_signals(bidding_signals);
  raw_request.set_enable_debug_reporting(enable_debug_reporting);
  raw_request.set_seller(kSeller);
  raw_request.set_publisher_name(kPublisherName);
}

TEST_F(GenerateBidsReactorTest, DoesNotGenerateBidsForIGWithNoAds) {
  Response ads;
  RawRequest raw_request;
  IGForBidding baz;
  baz.set_name("baz");
  std::vector<IGForBidding> igs;
  igs.push_back(std::move(baz));
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);

  EXPECT_CALL(dispatcher_, BatchExecute).Times(0);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, DoesNotGenerateBidsForIGWithNoBiddingSignals) {
  Response ads;
  RawRequest raw_request;
  auto ig_for_bidding_foo = GetIGForBiddingFoo();
  ig_for_bidding_foo.mutable_trusted_bidding_signals_keys()->Clear();
  std::vector<IGForBidding> igs;
  igs.push_back(ig_for_bidding_foo);
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);

  EXPECT_CALL(dispatcher_, BatchExecute).Times(0);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, GenerateBidSuccessfulWithCodeWrapper) {
  bool enable_debug_reporting = false;
  bool enable_buyer_debug_url_generation = false;
  bool enable_adtech_code_logging = true;
  std::string json = GetTestResponse(kTestRenderUrl, 1);
  AdWithBid bid;
  bid.set_render(kTestRenderUrl);
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json](std::vector<DispatchRequest>& batch,
                       BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  rawRequest, enable_debug_reporting);
  CheckGenerateBids(rawRequest, ads, enable_buyer_debug_url_generation,
                    enable_adtech_code_logging);
}

TEST_F(GenerateBidsReactorTest,
       DoesNotGenerateBidsIfBiddingSignalsAreMalformed) {
  auto in_signals = R"JSON({"Bar":1})JSON";
  Response ads;
  RawRequest raw_request;
  IGForBidding foo, bar;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  igs.push_back(GetIGForBiddingBar());
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, in_signals,
                  raw_request);

  EXPECT_CALL(dispatcher_, BatchExecute).Times(0);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, GeneratesBidForSingleIGForBidding) {
  std::string json = GetTestResponse(kTestRenderUrl, 1);
  AdWithBid bid;
  bid.set_render(kTestRenderUrl);
  bid.set_bid(1);
  bid.set_interest_group_name("Foo");
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json](std::vector<DispatchRequest>& batch,
                       BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  RawRequest raw_request;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, IGSerializationLatencyBenchmark) {
  std::string generate_bids_response_for_mock =
      GetTestResponse(kTestRenderUrl, 1);

  Response ads;
  std::vector<IGForBidding> igs;
  int num_igs = 10;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  for (int i = 0; i < num_igs; i++) {
    auto ig = MakeALargeInterestGroupForBiddingForLatencyTesting();
    // Add a key so the IG will have some trusted bidding signals so it will be
    // bid upon.
    ig.mutable_trusted_bidding_signals_keys()->Add("bidding_signal");

    AdWithBid bid;
    bid.set_render(kTestRenderUrl);
    bid.set_bid(1);
    bid.set_interest_group_name(ig.name());

    *raw_response.add_bids() = bid;
    igs.push_back(std::move(ig));
  }
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([generate_bids_response_for_mock](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback),
                           generate_bids_response_for_mock);
      });
  RawRequest raw_request;

  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, GeneratesBidsForMultipleIGForBiddings) {
  std::string json = GetTestResponse(kTestRenderUrl, 1);
  AdWithBid bidA;
  bidA.set_render(kTestRenderUrl);
  bidA.set_bid(1);
  bidA.set_interest_group_name("Bar");

  AdWithBid bidB;
  bidB.set_render(kTestRenderUrl);
  bidB.set_bid(1);
  bidB.set_interest_group_name("Foo");

  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bidA;
  *raw_response.add_bids() = bidB;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json](std::vector<DispatchRequest>& batch,
                       BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  ASSERT_EQ(raw_response.bids().size(), 2);
  // Expect bids differentiated by interest_group name.
  RawRequest raw_request;
  IGForBidding foo, bar;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());
  igs.push_back(GetIGForBiddingFoo());
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, FiltersBidsWithZeroBidPrice) {
  std::vector<std::string> json_arr{GetTestResponse(kTestRenderUrl, 1),
                                    GetTestResponse(kTestRenderUrl, 0)};
  AdWithBid bidA;
  bidA.set_render(kTestRenderUrl);
  bidA.set_bid(1);
  bidA.set_interest_group_name("Bar");

  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bidA;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json_arr](std::vector<DispatchRequest>& batch,
                           BatchDispatchDoneCallback batch_callback) {
        std::vector<absl::StatusOr<DispatchResponse>> responses;
        EXPECT_EQ(batch.size(), 2);
        for (int i = 0; i < 2; i++) {
          const auto& request = batch[i];
          EXPECT_EQ(request.handler_name, "generateBidEntryFunction");
          DispatchResponse dispatch_response = {};
          dispatch_response.resp = json_arr[i];
          dispatch_response.id = request.id;
          responses.emplace_back(dispatch_response);
        }
        batch_callback(responses);
        return absl::OkStatus();
      });
  ASSERT_EQ(raw_response.bids().size(), 1);
  // Expect bids differentiated by interest_group name.
  RawRequest raw_request;
  IGForBidding foo, bar;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());
  igs.push_back(GetIGForBiddingFoo());
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, CreatesGenerateBidInputsInCorrectOrder) {
  std::string json = GetTestResponse(kTestRenderUrl, 1);

  AdWithBid bid;
  bid.set_render(kTestRenderUrl);
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json](std::vector<DispatchRequest>& batch,
                       BatchDispatchDoneCallback batch_callback) {
        auto input = batch.at(0).input;
        EXPECT_EQ(input.size(), 6);
        if (input.size() == 6) {
          CheckCorrectnessOfBar(*input[0]);
          EXPECT_EQ(*input[1], R"JSON({"auction_signal": "test 1"})JSON");
          EXPECT_EQ(*input[2], R"JSON({"buyer_signal": "test 2"})JSON");
          EXPECT_EQ(*input[3], R"JSON({"bidding_signal":"test 3"})JSON");
          EXPECT_EQ(*input[4], bar_browser_signals);
        }
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  rawRequest);
  CheckGenerateBids(rawRequest, ads);
}

TEST_F(GenerateBidsReactorTest,
       GeneratesBidAndBuildsSignalsForIGNameBiddingSignals) {
  auto in_signals = R"JSON({"keys": {"Bar":1}, "perInterestGroupData":{}})JSON";
  auto expected_signals = R"JSON({"Bar":1})JSON";

  AdWithBid bid;
  bid.set_render("test.com");
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  std::string json = GetTestResponse(kTestRenderUrl, 1);
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce(
          [json, expected_signals](std::vector<DispatchRequest>& batch,
                                   BatchDispatchDoneCallback batch_callback) {
            auto input = batch.at(0).input;
            IGForBidding received;
            EXPECT_EQ(*input[3], expected_signals);
            return FakeExecute(batch, std::move(batch_callback), json);
          });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, in_signals,
                  rawRequest);
  CheckGenerateBids(rawRequest, ads, false);
}

// TODO (b/288954720): Once android signals message is defined and signals are
//  required, change this test to expect to fail.
TEST_F(GenerateBidsReactorTest, GeneratesBidDespiteNoBrowserSignals) {
  auto in_signals = R"JSON({"keys": {"Bar":1}, "perInterestGroupData":{}})JSON";
  auto expected_signals = R"JSON({"Bar":1})JSON";

  AdWithBid bid;
  bid.set_render("test.com");
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar(false));

  std::string json = GetTestResponse(kTestRenderUrl, 1);
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce(
          [json, expected_signals](std::vector<DispatchRequest>& batch,
                                   BatchDispatchDoneCallback batch_callback) {
            auto input = batch.at(0).input;
            IGForBidding received;
            EXPECT_EQ(*input[3], expected_signals);
            // Check that browser signals are an empty JSON string.
            EXPECT_EQ(*input[4], R"JSON("")JSON");
            return FakeExecute(batch, std::move(batch_callback), json);
          });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, in_signals,
                  rawRequest);
  CheckGenerateBids(rawRequest, ads, false);
}

TEST_F(GenerateBidsReactorTest, HandlesDuplicateKeysInBiddingSignalsKeys) {
  auto in_signals = R"JSON({"keys": {"Bar":1}, "perInterestGroupData":{}})JSON";
  auto expected_signals = R"JSON({"Bar":1})JSON";

  AdWithBid bid;
  bid.set_render(kTestRenderUrl);
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  auto bar = GetIGForBiddingBar();
  // Add name to bidding signals keys as well (duplicate lookup).
  bar.add_trusted_bidding_signals_keys(bid.interest_group_name());
  igs.push_back(std::move(bar));

  std::string json = GetTestResponse(kTestRenderUrl, 1);
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce(
          [json, expected_signals](std::vector<DispatchRequest>& batch,
                                   BatchDispatchDoneCallback batch_callback) {
            auto input = batch.at(0).input;
            IGForBidding received;
            EXPECT_EQ(*input[3], expected_signals);
            return FakeExecute(batch, std::move(batch_callback), json);
          });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, in_signals,
                  rawRequest);
  CheckGenerateBids(rawRequest, ads, false);
}

TEST_F(GenerateBidsReactorTest, GenerateBidResponseWithDebugUrls) {
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = true;
  const std::string json = R"JSON(
    {
      "response": {
        "render": "test.com",
        "bid": 1,
        "debug_report_urls": {
          "auction_debug_loss_url": "test.com/debugLoss",
          "auction_debug_win_url": "test.com/debugWin"
        }
      },
      "logs": []
    }
  )JSON";

  AdWithBid bid;
  bid.set_render(kTestRenderUrl);
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  DebugReportUrls debug_report_urls;
  debug_report_urls.set_auction_debug_win_url("test.com/debugWin");
  debug_report_urls.set_auction_debug_loss_url("test.com/debugLoss");
  *bid.mutable_debug_report_urls() = debug_report_urls;

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json](std::vector<DispatchRequest>& batch,
                       BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  rawRequest, enable_debug_reporting);
  CheckGenerateBids(rawRequest, ads, enable_buyer_debug_url_generation);
}

TEST_F(GenerateBidsReactorTest, GenerateBidResponseWithoutDebugUrls) {
  bool enable_debug_reporting = true;
  bool enable_buyer_debug_url_generation = false;
  std::string json = GetTestResponse(kTestRenderUrl, 1);

  AdWithBid bid;
  bid.set_render(kTestRenderUrl);
  bid.set_bid(1);
  bid.set_interest_group_name("Bar");
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json](std::vector<DispatchRequest>& batch,
                       BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  RawRequest rawRequest;
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  rawRequest, enable_debug_reporting);
  CheckGenerateBids(rawRequest, ads, enable_buyer_debug_url_generation);
}

TEST_F(GenerateBidsReactorTest, AddsTrustedBiddingSignalsKeysToScriptInput) {
  Response response;
  RawRequest raw_request;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  std::string json = GetTestResponse(kTestRenderUrl, 1);
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  absl::Notification notification;
  // Verify that serialized IG contains trustedBiddingSignalKeys.
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&notification, json](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback batch_callback) {
        EXPECT_EQ(batch.size(), 1);
        EXPECT_GT(batch.at(0).input.size(), 0);
        IGForBidding ig_for_bidding;
        std::string actual_first_ig_as_str = batch.at(0).input.at(0)->c_str();
        CheckForAndReplaceUBSWithEmptyString(actual_first_ig_as_str,
                                             kUserBiddingSignals);
        EXPECT_TRUE(google::protobuf::util::JsonStringToMessage(
                        actual_first_ig_as_str, &ig_for_bidding)
                        .ok());
        EXPECT_EQ(ig_for_bidding.trusted_bidding_signals_keys_size(), 1);
        EXPECT_STREQ(ig_for_bidding.trusted_bidding_signals_keys(0).c_str(),
                     "bidding_signal");
        notification.Notify();
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
      std::make_unique<BiddingNoOpLogger>();

  BiddingServiceRuntimeConfig runtime_config = {
      .encryption_enabled = true,
      .enable_buyer_debug_url_generation = false,
  };
  GenerateBidsReactor reactor(dispatcher_, &request_, &response,
                              std::move(benchmarkingLogger),
                              key_fetcher_manager_.get(), crypto_client_.get(),
                              std::move(runtime_config));
  reactor.Execute();
  notification.WaitForNotification();
}

TEST_F(GenerateBidsReactorTest,
       AddsTrustedBiddingSignalsKeysToScriptInput_EncryptionEnabled) {
  std::string json = GetTestResponse(kTestRenderUrl, 1);
  Response response;
  RawRequest raw_request;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  BuildRawRequest(igs, testAuctionSignals, testBuyerSignals, testBiddingSignals,
                  raw_request);
  request_.set_request_ciphertext(raw_request.SerializeAsString());

  absl::Notification notification;
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([json, &notification](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback batch_callback) {
        EXPECT_EQ(batch.size(), 1);
        EXPECT_GT(batch.at(0).input.size(), 0);
        IGForBidding ig_for_bidding;
        std::string actual_first_ig_as_str = batch.at(0).input.at(0)->c_str();
        CheckForAndReplaceUBSWithEmptyString(actual_first_ig_as_str,
                                             kUserBiddingSignals);
        EXPECT_TRUE(google::protobuf::util::JsonStringToMessage(
                        actual_first_ig_as_str, &ig_for_bidding)
                        .ok());
        EXPECT_EQ(ig_for_bidding.trusted_bidding_signals_keys_size(), 1);
        EXPECT_STREQ(ig_for_bidding.trusted_bidding_signals_keys(0).c_str(),
                     "bidding_signal");
        notification.Notify();
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
      std::make_unique<BiddingNoOpLogger>();

  BiddingServiceRuntimeConfig runtime_config = {.encryption_enabled = true};
  GenerateBidsReactor reactor(dispatcher_, &request_, &response,
                              std::move(benchmarkingLogger),
                              key_fetcher_manager_.get(), crypto_client_.get(),
                              std::move(runtime_config));
  reactor.Execute();
  notification.WaitForNotification();

  EXPECT_FALSE(response.response_ciphertext().empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
