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

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/generate_bids_reactor_test_utils.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

// Bidding signals must be contained in "keys" in root object.
constexpr char kTestTrustedBiddingSignals[] =
    R"json({"trusted_bidding_signal_key": "some_trusted_bidding_signal_value"})json";
constexpr char kTopLevelSeller[] = "https://www.example-top-ssp.com";
constexpr char kUserBiddingSignals[] =
    R"JSON({"userBiddingSignalKey": 123})JSON";
constexpr char bar_browser_signals[] =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","joinCount":5,"bidCount":25,"recency":1684134093000,"prevWins":[[1,"1868"],[1,"1954"]],"dataVersion":1787})json";
constexpr char kExpectedBrowserSignalsWithRecencyMs[] =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","joinCount":5,"bidCount":25,"recency":123456000,"prevWins":[[1,"1868"],[1,"1954"]],"dataVersion":1787})json";
absl::string_view kComponentBrowserSignals =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","topLevelSeller":"https://www.example-top-ssp.com","joinCount":5,"bidCount":25,"recency":1684134092000,"prevWins":[[1,"1689"],[1,"1776"]],"dataVersion":1787,"multiBidLimit":2})json";
absl::string_view kComponentBrowserSignalsWithRecencyMs =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","topLevelSeller":"https://www.example-top-ssp.com","joinCount":5,"bidCount":25,"recency":123456000,"prevWins":[[1,"1689"],[1,"1776"]],"dataVersion":1787,"multiBidLimit":2})json";
absl::string_view kComponentBrowserSignalsWithMultiBidLimit =
    R"json({"topWindowHostname":"www.example-publisher.com","seller":"https://www.example-ssp.com","topLevelSeller":"https://www.example-top-ssp.com","joinCount":5,"bidCount":25,"recency":1684134092000,"prevWins":[[1,"1689"],[1,"1776"]],"dataVersion":1787,"multiBidLimit":3})json";
using ::google::protobuf::TextFormat;

using ::google::protobuf::util::MessageToJsonString;

using Request = GenerateBidsRequest;
using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using Response = GenerateBidsResponse;
using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;

absl::Status FakeExecute(std::vector<DispatchRequest>& batch,
                         BatchDispatchDoneCallback batch_callback,
                         absl::string_view response_json) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;
  for (const auto& request : batch) {
    EXPECT_EQ(request.handler_name, "generateBidEntryFunction");
    DispatchResponse dispatch_response = {};
    dispatch_response.resp = response_json;
    dispatch_response.id = request.id;
    responses.emplace_back(dispatch_response);
  }
  batch_callback(responses);
  return absl::OkStatus();
}

std::string GetTestResponse(absl::string_view render, float bid,
                            bool enable_adtech_code_logging = false) {
  if (enable_adtech_code_logging) {
    return absl::Substitute(R"JSON({
      "response": [{
        "render": "$0",
        "bid": $1
      }],
      "logs": ["test log"],
      "errors": ["test.error"],
      "warnings":["test.warn"]
    })JSON",
                            render, bid);
  }

  return absl::Substitute(R"JSON([{
    "render": "$0",
    "bid": $1
  }])JSON",
                          render, bid);
}

std::string GetTestResponseWithPAgg(
    absl::string_view render, float bid,
    const PrivateAggregateContribution& privateAggregationContribution,
    bool enable_adtech_code_logging = false) {
  auto options = google::protobuf::util::JsonPrintOptions();
  options.preserve_proto_field_names = true;
  std::string json_contribution;
  CHECK_OK(google::protobuf::util::MessageToJsonString(
      privateAggregationContribution, &json_contribution, options));

  if (enable_adtech_code_logging) {
    return absl::Substitute(R"JSON({
      "response": [{
        "render": "$0",
        "bid": $1,
        "private_aggregation_contributions": [$2]
      }],
      "logs": ["test log"],
      "errors": ["test.error"],
      "warnings":["test.warn"]
    })JSON",
                            render, bid, json_contribution);
  }
  return absl::Substitute(R"JSON([{
    "render": "$0",
    "bid": $1,
    "private_aggregation_contributions": [$2]
  }])JSON",
                          render, bid, json_contribution);
}

std::string GetTestResponseWithReportingIds(
    absl::string_view render, float bid, absl::string_view buyer_reporting_id,
    absl::string_view bas_reporting_id, absl::string_view sbas_reporting_id,
    bool enable_adtech_code_logging = false) {
  if (enable_adtech_code_logging) {
    return absl::Substitute(R"JSON({
      "response": [{
        "render": "$0",
        "bid": $1,
        "buyerReportingId": "$2",
        "buyerAndSellerReportingId": "$3",
        "selectedBuyerAndSellerReportingId": "$4"
      }],
      "logs": [],
      "errors": [],
      "warnings":[]
    })JSON",
                            render, bid, buyer_reporting_id, bas_reporting_id,
                            sbas_reporting_id);
  }

  return absl::Substitute(R"JSON([{
    "render": "$0",
    "bid": $1,
    "buyerReportingId": "$2",
    "buyerAndSellerReportingId": "$3",
    "selectedBuyerAndSellerReportingId": "$4"
  }])JSON",
                          render, bid, buyer_reporting_id, bas_reporting_id,
                          sbas_reporting_id);
}

std::string GetTestResponseWithUnknownField(
    absl::string_view render, float bid,
    bool enable_adtech_code_logging = false) {
  if (enable_adtech_code_logging) {
    return absl::Substitute(R"JSON({
      "response": [{
        "render": "$0",
        "bid": $1,
        "buyer_reporting_ids": "abcdef"
      }],
      "logs": [],
      "errors": [],
      "warnings":[]
    })JSON",
                            render, bid);
  }

  return absl::Substitute(R"JSON([{
    "render": "$0",
    "bid": $1,
    "buyer_reporting_ids": "abcdef"
  }])JSON",
                          render, bid);
}

std::string GetComponentAuctionResponse(
    absl::string_view render, float bid, bool allow_component_auction,
    bool enable_adtech_code_logging = false) {
  if (enable_adtech_code_logging) {
    return absl::Substitute(R"JSON({
      "response": [{
        "render": "$0",
        "bid": $1,
        "allowComponentAuction": $2
      }],
      "logs": ["test log"],
      "errors": ["test.error"],
      "warnings":["test.warn"]
    })JSON",
                            render, bid, allow_component_auction);
  }

  return absl::Substitute(R"JSON([{
    "render": "$0",
    "bid": $1,
    "allowComponentAuction": $2
  }])JSON",
                          render, bid, allow_component_auction);
}

// TODO(b/257649113): Incorporate new fields in InterestGroupForBidding.
class GenerateBidsReactorTest : public testing::Test {
 public:
  MockV8DispatchClient dispatcher_;

 protected:
  void SetUp() override {
    // initialize
    CommonTestInit();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);
    server_common::log::ServerToken(kTestConsentToken);

    TrustedServersConfigClient config_client({});
    config_client.SetOverride(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(
        config_client, /* public_key_fetcher= */ nullptr);
    SetupMockCryptoClientWrapper(*crypto_client_);
    request_.set_key_id(kKeyId);
    auto raw_request = MakeARandomGenerateBidsRawRequestForAndroid();
    request_.set_request_ciphertext(raw_request.SerializeAsString());
  }

  void CheckGenerateBids(const RawRequest& raw_request,
                         const Response& expected_response,
                         std::optional<BiddingServiceRuntimeConfig>
                             runtime_config = std::nullopt) {
    Response response;
    std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
        std::make_unique<BiddingNoOpLogger>();
    if (!runtime_config) {
      runtime_config = {
          .enable_buyer_debug_url_generation = false,
      };
    }
    request_.set_request_ciphertext(raw_request.SerializeAsString());
    grpc::CallbackServerContext context;
    GenerateBidsReactor reactor(&context, dispatcher_, &request_, &response,
                                std::move(benchmarkingLogger),
                                key_fetcher_manager_.get(),
                                crypto_client_.get(), *runtime_config);
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
    EXPECT_TRUE(diff.Compare(expected_raw_response, raw_response))
        << diff_output;
  }

  Request request_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
};

constexpr char kTrustedBiddingSignalKey[] = "trusted_bidding_signal_key";
constexpr uint32_t kDataVersionForAll = 1787;

constexpr absl::string_view kIgNameFoo = "ig_name_Foo";
constexpr char kIgFooFirstAdRenderId[] = "1689";
constexpr char kIgFooSecondAdRenderId[] = "1776";

constexpr absl::string_view kIgNameBar = "ig_name_Bar";
constexpr char kIgBarFirstAdRenderId[] = "1868";
constexpr char kIgBarSecondAdRenderId[] = "1954";

IGForBidding GetIGForBiddingFoo() {
  IGForBidding interest_group;
  interest_group.set_name(kIgNameFoo);
  interest_group.set_user_bidding_signals(kUserBiddingSignals);
  interest_group.mutable_trusted_bidding_signals_keys()->Add(
      kTrustedBiddingSignalKey);
  interest_group.set_trusted_bidding_signals(kTestTrustedBiddingSignals);
  interest_group.mutable_ad_render_ids()->Add(kIgFooFirstAdRenderId);
  interest_group.mutable_ad_render_ids()->Add(kIgFooSecondAdRenderId);

  BrowserSignals browser_signals;
  interest_group.mutable_browser_signals()->set_join_count(5);
  interest_group.mutable_browser_signals()->set_bid_count(25);
  interest_group.mutable_browser_signals()->set_recency(1684134092);
  interest_group.mutable_browser_signals()->set_prev_wins(
      MakeRandomPreviousWins(interest_group.ad_render_ids(), true));
  return interest_group;
}

IGForBidding GetIGForBiddingBar(bool make_browser_signals = true) {
  IGForBidding interest_group;
  interest_group.set_name(kIgNameBar);
  interest_group.set_user_bidding_signals(kUserBiddingSignals);
  interest_group.mutable_trusted_bidding_signals_keys()->Add(
      kTrustedBiddingSignalKey);
  interest_group.set_trusted_bidding_signals(kTestTrustedBiddingSignals);
  interest_group.mutable_ad_render_ids()->Add(kIgBarFirstAdRenderId);
  interest_group.mutable_ad_render_ids()->Add(kIgBarSecondAdRenderId);

  if (make_browser_signals) {
    interest_group.mutable_browser_signals()->set_join_count(5);
    interest_group.mutable_browser_signals()->set_bid_count(25);
    interest_group.mutable_browser_signals()->set_recency(1684134093);
    interest_group.mutable_browser_signals()->set_prev_wins(
        MakeRandomPreviousWins(interest_group.ad_render_ids(), true));
  }
  return interest_group;
}

AdWithBid GetAdWithBidFromIgFoo(absl::string_view ad_render_url, int bid) {
  AdWithBid bid_from_foo;
  bid_from_foo.set_render(ad_render_url);
  bid_from_foo.set_bid(bid);
  bid_from_foo.set_interest_group_name(kIgNameFoo);
  bid_from_foo.set_data_version(kDataVersionForAll);
  return bid_from_foo;
}

AdWithBid GetAdWithBidFromIgBar(absl::string_view ad_render_url, int bid) {
  AdWithBid bid_from_bar;
  bid_from_bar.set_render(ad_render_url);
  bid_from_bar.set_bid(bid);
  bid_from_bar.set_interest_group_name(kIgNameBar);
  bid_from_bar.set_data_version(kDataVersionForAll);
  return bid_from_bar;
}

// Allows re-serialization.
void CheckForAndReplaceUBSWithEmptyString(
    std::string& serialized_ig, absl::string_view user_bidding_signals) {
  // Check for the presence of the correct user bidding signals
  auto index_of_ubs = serialized_ig.find(user_bidding_signals);
  EXPECT_NE(index_of_ubs, std::string::npos);
  // UBS will not deserialize into a string (hence the custom serialization
  // logic, so we excise it from the string before going back to a message.
  serialized_ig.replace(index_of_ubs, user_bidding_signals.length(),
                        R"JSON("")JSON");
}

void CheckCorrectnessOfIg(std::string& serialized_actual,
                          IGForBidding expected) {
  CheckForAndReplaceUBSWithEmptyString(serialized_actual, kUserBiddingSignals);
  IGForBidding reconstituted_actual_ig;
  // Re-create a Message to run the rest of the checking on (since fields may be
  // serialized in non-deterministic orders).
  CHECK_OK(google::protobuf::util::JsonStringToMessage(
      serialized_actual, &reconstituted_actual_ig))
      << "Could not reconstitute IG: " << serialized_actual;
  // Expected IG needs trusted bidding signals and device signals cleared since
  // they will not be in the actual bar. These are not passed as part of the
  // serialized IG, but as separate parameters to GenerateBid.
  expected.clear_DeviceSignals();
  expected.clear_trusted_bidding_signals();
  // Since UBS will not be equal after re-serialization, clear those as well in
  // both.
  reconstituted_actual_ig.clear_user_bidding_signals();
  expected.clear_user_bidding_signals();
  bool match = google::protobuf::util::MessageDifferencer::Equals(
      expected, reconstituted_actual_ig);
  EXPECT_TRUE(match);
  if (!match) {
    std::string expected_as_str, actual_for_comparison_as_str;
    CHECK_OK(MessageToJsonString(expected, &expected_as_str));
    CHECK_OK(MessageToJsonString(reconstituted_actual_ig,
                                 &actual_for_comparison_as_str));
    ABSL_LOG(INFO) << "\nExpected:\n"
                   << expected_as_str << "\nActual:\n"
                   << actual_for_comparison_as_str;
  }
}

struct RawRequestOptions {
  std::vector<IGForBidding> interest_groups_to_add;
  bool enable_debug_reporting = false;
  bool enable_adtech_code_logging = false;
  absl::string_view auction_signals = kTestAuctionSignals;
  absl::string_view buyer_signals = kTestBuyerSignals;
  absl::string_view seller = kTestSeller;
  absl::string_view publisher_name = kTestPublisherName;
  uint32_t data_version = kDataVersionForAll;
  int multi_bid_limit = kDefaultMultiBidLimit;
};

RawRequest BuildRawRequest(const RawRequestOptions& options) {
  RawRequest raw_request;
  for (int i = 0; i < options.interest_groups_to_add.size(); i++) {
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        options.interest_groups_to_add[i];
  }
  raw_request.set_auction_signals(options.auction_signals);
  raw_request.mutable_blob_versions()->set_protected_audience_generate_bid_udf(
      "pa/generateBid");
  raw_request.set_buyer_signals(options.buyer_signals);
  raw_request.set_enable_debug_reporting(options.enable_debug_reporting);
  raw_request.set_seller(options.seller);
  raw_request.set_publisher_name(options.publisher_name);
  raw_request.set_data_version(options.data_version);
  if (options.enable_adtech_code_logging) {
    raw_request.mutable_consented_debug_config()->set_token(kTestConsentToken);
    raw_request.mutable_consented_debug_config()->set_is_consented(true);
  }
  raw_request.set_multi_bid_limit(options.multi_bid_limit);
  return raw_request;
}

RawRequest BuildRawRequestForComponentAuction(
    const RawRequestOptions& options,
    absl::string_view top_level_seller = kTopLevelSeller) {
  RawRequest raw_request = BuildRawRequest(options);
  raw_request.set_top_level_seller(top_level_seller);
  return raw_request;
}

TEST_F(GenerateBidsReactorTest, GenerateBidSuccessfulWithCodeWrapper) {
  bool enable_adtech_code_logging = true;
  const std::string response_json =
      GetTestResponse(kTestRenderUrl, 1, enable_adtech_code_logging);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  raw_response.set_bidding_export_debug(true);
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  RawRequest raw_request = BuildRawRequest({
      .interest_groups_to_add = std::move(igs),
      .enable_adtech_code_logging = enable_adtech_code_logging,
  });
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, PrivateAggregationObjectSetInResponse) {
  bool enable_adtech_code_logging = true;
  PrivateAggregateContribution pAggContribution =
      CreateTestPAggContribution(EVENT_TYPE_WIN,
                                 /* event_name = */ "");
  std::string response_json =
      GetTestResponseWithPAgg(kTestRenderUrl, /* bid = */ 1.0, pAggContribution,
                              enable_adtech_code_logging);
  AdWithBid bid = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  *bid.add_private_aggregation_contributions() = std::move(pAggContribution);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = std::move(bid);
  raw_response.set_bidding_export_debug(true);
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs),
                                     .enable_adtech_code_logging =
                                         enable_adtech_code_logging}),
                    ads);
}

TEST_F(GenerateBidsReactorTest, ReportingIdsSetInResponse) {
  bool enable_adtech_code_logging = true;
  std::string response_json = GetTestResponseWithReportingIds(
      kTestRenderUrl, 1, kTestBuyerReportingId, kTestBuyerAndSellerReportingId,
      kTestSelectedBuyerAndSellerReportingId, enable_adtech_code_logging);
  AdWithBid bid = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  bid.set_buyer_reporting_id(kTestBuyerReportingId);
  bid.set_buyer_and_seller_reporting_id(kTestBuyerAndSellerReportingId);
  bid.set_selected_buyer_and_seller_reporting_id(
      kTestSelectedBuyerAndSellerReportingId);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  RawRequest raw_request = BuildRawRequest({
      .interest_groups_to_add = std::move(igs),
      .enable_adtech_code_logging = enable_adtech_code_logging,
  });
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, UnknownFieldInResponseParsedSuccessfully) {
  bool enable_adtech_code_logging = true;
  std::string response_json = GetTestResponseWithUnknownField(
      kTestRenderUrl, 1, enable_adtech_code_logging);
  AdWithBid bid = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  RawRequest raw_request = BuildRawRequest({
      .interest_groups_to_add = std::move(igs),
      .enable_adtech_code_logging = enable_adtech_code_logging,
  });
  CheckGenerateBids(raw_request, ads);
}

TEST_F(GenerateBidsReactorTest, DoesNotValidateBiddingSignalsStructure) {
  Response ads;
  IGForBidding foo = GetIGForBiddingFoo();
  foo.set_trusted_bidding_signals("Invalid JSON");
  std::vector<IGForBidding> igs;
  igs.push_back(foo);
  EXPECT_CALL(dispatcher_, BatchExecute).Times(igs.size());
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    ads);
}

TEST_F(GenerateBidsReactorTest, GeneratesBidForSingleIGForBidding) {
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);
  AdWithBid bid = GetAdWithBidFromIgFoo(kTestRenderUrl, 1);
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    ads);
}

TEST_F(GenerateBidsReactorTest, IGSerializationLatencyBenchmark) {
  const std::string generate_bids_response_for_mock =
      GetTestResponse(kTestRenderUrl, 1);

  Response ads;
  std::vector<IGForBidding> igs;
  int num_igs = 10;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  for (int i = 0; i < num_igs; i++) {
    auto ig = MakeALargeInterestGroupForBiddingForLatencyTesting();
    // Add a key so the IG will have some trusted bidding signals so it will be
    // bid upon.
    ig.mutable_trusted_bidding_signals_keys()->Add(
        "trusted_bidding_signal_key");

    AdWithBid bid;
    bid.set_render(kTestRenderUrl);
    bid.set_bid(1);
    bid.set_interest_group_name(ig.name());
    bid.set_data_version(kDataVersionForAll);
    *raw_response.add_bids() = bid;
    igs.push_back(std::move(ig));
  }
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&generate_bids_response_for_mock](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback),
                           generate_bids_response_for_mock);
      });
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    ads);
}

TEST_F(GenerateBidsReactorTest, GeneratesBidsForMultipleIGForBiddings) {
  GenerateBidsResponse expected_response;
  GenerateBidsResponse::GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = GetAdWithBidFromIgFoo(kTestRenderUrl, 1);
  *expected_raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  ASSERT_EQ(expected_raw_response.bids().size(), 2);
  *expected_response.mutable_response_ciphertext() =
      expected_raw_response.SerializeAsString();

  const std::string response_json_for_mock = GetTestResponse(kTestRenderUrl, 1);
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce(
          [&response_json_for_mock](std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback batch_callback) {
            return FakeExecute(batch, std::move(batch_callback),
                               response_json_for_mock);
          });

  // Expect bids differentiated by interest_group name.
  IGForBidding foo, bar;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());
  igs.push_back(GetIGForBiddingFoo());
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    expected_response);
}

TEST_F(GenerateBidsReactorTest, FiltersBidsWithZeroBidPrice) {
  const std::vector<std::string> json_arr{GetTestResponse(kTestRenderUrl, 1),
                                          GetTestResponse(kTestRenderUrl, 0)};
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&json_arr](std::vector<DispatchRequest>& batch,
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
  IGForBidding foo, bar;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());
  igs.push_back(GetIGForBiddingFoo());
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    ads);
}

TEST_F(GenerateBidsReactorTest, CreatesGenerateBidInputsInCorrectOrder) {
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);
  Response expected_response;
  GenerateBidsResponse::GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  *expected_response.mutable_response_ciphertext() =
      expected_raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        auto input = batch.at(0).input;
        EXPECT_EQ(input.size(), 7);
        if (input.size() == 6) {
          CheckCorrectnessOfIg(*input[0], GetIGForBiddingBar());
          EXPECT_EQ(*input[1], R"JSON({"auction_signal": "test 1"})JSON");
          EXPECT_EQ(*input[2], R"JSON({"buyer_signal": "test 2"})JSON");
          EXPECT_EQ(*input[3], kTestTrustedBiddingSignals);
          EXPECT_EQ(*input[4], bar_browser_signals);
        }
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    expected_response);
}

TEST_F(GenerateBidsReactorTest, RespectsPerRequestBlobVersioning) {
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);
  Response expected_response;
  GenerateBidsResponse::GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  *expected_response.mutable_response_ciphertext() =
      expected_raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  auto raw_request =
      BuildRawRequest({.interest_groups_to_add = std::move(igs)});

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json, &raw_request](
                    std::vector<DispatchRequest>& batch,
                    BatchDispatchDoneCallback batch_callback) {
        EXPECT_EQ(
            batch[0].version_string,
            raw_request.blob_versions().protected_audience_generate_bid_udf());
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(
      raw_request, expected_response,
      BiddingServiceRuntimeConfig({.use_per_request_udf_versioning = true}));
}

TEST_F(GenerateBidsReactorTest,
       CreatesGenerateBidInputsInCorrectOrderWithRecencyMs) {
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);

  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  auto ig = GetIGForBiddingBar();
  ig.mutable_browser_signals()->set_recency_ms(123456000);
  igs.push_back(std::move(ig));

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        auto input = batch.at(0).input;
        EXPECT_EQ(input.size(), 7);
        if (input.size() == 6) {
          CheckCorrectnessOfIg(*input[0], GetIGForBiddingBar());
          EXPECT_EQ(*input[1], R"JSON({"auction_signal": "test 1"})JSON");
          EXPECT_EQ(*input[2], R"JSON({"buyer_signal": "test 2"})JSON");
          EXPECT_EQ(*input[3], kTestTrustedBiddingSignals);
          EXPECT_EQ(*input[4], kExpectedBrowserSignalsWithRecencyMs);
        }
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    ads);
}

TEST_F(GenerateBidsReactorTest,
       CreatesGenerateBidInputsCorrectlyForComponentAuction) {
  std::string json = GetComponentAuctionResponse(
      kTestRenderUrl, /*bid=*/1, /*allow_component_auction=*/true);
  InterestGroupForBidding ig_foo = GetIGForBiddingFoo();
  AdWithBid bid = GetAdWithBidFromIgFoo(kTestRenderUrl, 1);
  bid.set_allow_component_auction(true);
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs{ig_foo};

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&json, &ig_foo](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        // Test setup check.
        CHECK_EQ(batch.size(), 1)
            << absl::InternalError("Test setup error. Batch size must be 1.");
        auto input = batch.at(0).input;
        CHECK_EQ(batch.size(), 1)
            << absl::InternalError("Test setup error. Input size must be 6.");
        CheckCorrectnessOfIg(*input[ArgIndex(GenerateBidArgs::kInterestGroup)],
                             ig_foo);
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kAuctionSignals)],
                  R"JSON({"auction_signal": "test 1"})JSON");
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kBuyerSignals)],
                  R"JSON({"buyer_signal": "test 2"})JSON");
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kTrustedBiddingSignals)],
                  kTestTrustedBiddingSignals);
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kDeviceSignals)],
                  kComponentBrowserSignals);
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  CheckGenerateBids(BuildRawRequestForComponentAuction({
                        .interest_groups_to_add = std::move(igs),
                    }),
                    ads);
}

TEST_F(GenerateBidsReactorTest,
       CreatesGenerateBidInputsCorrectlyForComponentAuctionWithRecencyMs) {
  std::string json = GetComponentAuctionResponse(
      kTestRenderUrl, /*bid=*/1, /*allow_component_auction=*/true);
  InterestGroupForBidding ig_foo = GetIGForBiddingFoo();
  AdWithBid bid = GetAdWithBidFromIgFoo(kTestRenderUrl, 1);
  bid.set_allow_component_auction(true);
  ig_foo.mutable_browser_signals()->set_recency_ms(123456000);
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs{ig_foo};

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&json, &ig_foo](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        // Test setup check.
        CHECK_EQ(batch.size(), 1)
            << absl::InternalError("Test setup error. Batch size must be 1.");
        auto input = batch.at(0).input;
        CHECK_EQ(batch.size(), 1)
            << absl::InternalError("Test setup error. Input size must be 6.");
        CheckCorrectnessOfIg(*input[ArgIndex(GenerateBidArgs::kInterestGroup)],
                             ig_foo);
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kAuctionSignals)],
                  R"JSON({"auction_signal": "test 1"})JSON");
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kBuyerSignals)],
                  R"JSON({"buyer_signal": "test 2"})JSON");
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kTrustedBiddingSignals)],
                  kTestTrustedBiddingSignals);
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kDeviceSignals)],
                  kComponentBrowserSignalsWithRecencyMs);
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  CheckGenerateBids(BuildRawRequestForComponentAuction({
                        .interest_groups_to_add = std::move(igs),
                    }),
                    ads);
}

TEST_F(GenerateBidsReactorTest,
       CreatesGenerateBidInputsCorrectlyForComponentAuctionWithMultiBidLimit) {
  std::string json = GetComponentAuctionResponse(
      kTestRenderUrl, /*bid=*/1, /*allow_component_auction=*/true);
  InterestGroupForBidding ig = GetIGForBiddingFoo();
  AdWithBid bid = GetAdWithBidFromIgFoo(kTestRenderUrl, 1);
  bid.set_allow_component_auction(true);
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs{ig};

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&json, &ig](std::vector<DispatchRequest>& batch,
                             BatchDispatchDoneCallback batch_callback) {
        // Test setup check.
        CHECK_EQ(batch.size(), 1)
            << absl::InternalError("Test setup error. Batch size must be 1.");
        auto input = batch.at(0).input;
        CHECK_EQ(batch.size(), 1)
            << absl::InternalError("Test setup error. Input size must be 6.");
        CheckCorrectnessOfIg(*input[ArgIndex(GenerateBidArgs::kInterestGroup)],
                             ig);
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kAuctionSignals)],
                  R"JSON({"auction_signal": "test 1"})JSON");
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kBuyerSignals)],
                  R"JSON({"buyer_signal": "test 2"})JSON");
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kTrustedBiddingSignals)],
                  kTestTrustedBiddingSignals);
        EXPECT_EQ(*input[ArgIndex(GenerateBidArgs::kDeviceSignals)],
                  kComponentBrowserSignalsWithMultiBidLimit);
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  CheckGenerateBids(
      BuildRawRequestForComponentAuction(
          {.interest_groups_to_add = std::move(igs), .multi_bid_limit = 3}),
      ads);
}

TEST_F(GenerateBidsReactorTest,
       ParsesAllowComponentAuctionFieldForComponentAuction) {
  std::string json = GetComponentAuctionResponse(
      kTestRenderUrl, /*bid=*/1, /*allow_component_auction=*/true);
  auto ig = GetIGForBiddingFoo();
  AdWithBid bid = GetAdWithBidFromIgFoo(kTestRenderUrl, 1);
  bid.set_allow_component_auction(true);
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = bid;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs{ig};

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&json](std::vector<DispatchRequest>& batch,
                        BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  CheckGenerateBids(BuildRawRequestForComponentAuction({
                        .interest_groups_to_add = std::move(igs),
                    }),
                    ads);
}

TEST_F(GenerateBidsReactorTest, SkipsUnallowedAdForComponentAuction) {
  std::string json = GetComponentAuctionResponse(
      kTestRenderUrl, /*bid=*/1, /*allow_component_auction=*/false);
  auto ig = GetIGForBiddingFoo();
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs{ig};

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&json](std::vector<DispatchRequest>& batch,
                        BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), json);
      });
  CheckGenerateBids(BuildRawRequestForComponentAuction({
                        .interest_groups_to_add = std::move(igs),
                    }),
                    ads);
}

// TODO (b/288954720): Once android signals message is defined and signals are
// required, change this test to expect to fail.
TEST_F(GenerateBidsReactorTest, GeneratesBidDespiteNoBrowserSignals) {
  Response ads;
  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() =
      GetAdWithBidFromIgBar("https://adTech.com/ad?id=123", 1);
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar(false));

  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        auto input = batch.at(0).input;
        IGForBidding received;
        // Check that device signals are an empty JSON object.
        EXPECT_EQ(*input[4], R"JSON({})JSON");
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(BuildRawRequest({.interest_groups_to_add = std::move(igs)}),
                    ads);
}

TEST_F(GenerateBidsReactorTest, GenerateBidResponseWithDebugUrls) {
  const std::string response_json = R"JSON(
    [{
      "render": "https://adTech.com/ad?id=123",
      "bid": 1,
      "debug_report_urls": {
        "auction_debug_loss_url": "test.com/debugLoss",
        "auction_debug_win_url": "test.com/debugWin"
      }
    }]
  )JSON";

  AdWithBid bid = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
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
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(
      BuildRawRequest({
          .interest_groups_to_add = std::move(igs),
          .enable_debug_reporting = true,
      }),
      ads,
      BiddingServiceRuntimeConfig({.enable_buyer_debug_url_generation = true}));
}

TEST_F(GenerateBidsReactorTest, GenerateBidResponseWithoutDebugUrls) {
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);

  GenerateBidsResponse::GenerateBidsRawResponse raw_response;
  *raw_response.add_bids() = GetAdWithBidFromIgBar(kTestRenderUrl, 1);
  Response ads;
  *ads.mutable_response_ciphertext() = raw_response.SerializeAsString();
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingBar());

  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json](std::vector<DispatchRequest>& batch,
                                 BatchDispatchDoneCallback batch_callback) {
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  CheckGenerateBids(BuildRawRequest({
                        .interest_groups_to_add = std::move(igs),
                        .enable_debug_reporting = true,
                    }),
                    ads);
}

TEST_F(GenerateBidsReactorTest, AddsTrustedBiddingSignalsKeysToScriptInput) {
  Response response;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);
  RawRequest raw_request =
      BuildRawRequest({.interest_groups_to_add = std::move(igs)});
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  absl::Notification notification;
  // Verify that serialized IG contains trustedBiddingSignalKeys.
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&notification, &response_json](
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
                     "trusted_bidding_signal_key");
        notification.Notify();
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  *request_.mutable_request_ciphertext() = raw_request.SerializeAsString();
  std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
      std::make_unique<BiddingNoOpLogger>();

  BiddingServiceRuntimeConfig runtime_config = {
      .enable_buyer_debug_url_generation = false,
  };
  grpc::CallbackServerContext context;
  GenerateBidsReactor reactor(&context, dispatcher_, &request_, &response,
                              std::move(benchmarkingLogger),
                              key_fetcher_manager_.get(), crypto_client_.get(),
                              runtime_config);
  reactor.Execute();
  notification.WaitForNotification();
}

TEST_F(GenerateBidsReactorTest,
       AddsTrustedBiddingSignalsKeysToScriptInput_EncryptionEnabled) {
  const std::string response_json = GetTestResponse(kTestRenderUrl, 1);
  Response response;
  std::vector<IGForBidding> igs;
  igs.push_back(GetIGForBiddingFoo());
  RawRequest raw_request =
      BuildRawRequest({.interest_groups_to_add = std::move(igs)});
  request_.set_request_ciphertext(raw_request.SerializeAsString());

  absl::Notification notification;
  EXPECT_CALL(dispatcher_, BatchExecute)
      .WillOnce([&response_json, &notification](
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
                     "trusted_bidding_signal_key");
        notification.Notify();
        return FakeExecute(batch, std::move(batch_callback), response_json);
      });
  std::unique_ptr<BiddingBenchmarkingLogger> benchmarkingLogger =
      std::make_unique<BiddingNoOpLogger>();

  BiddingServiceRuntimeConfig runtime_config;
  grpc::CallbackServerContext context;
  GenerateBidsReactor reactor(&context, dispatcher_, &request_, &response,
                              std::move(benchmarkingLogger),
                              key_fetcher_manager_.get(), crypto_client_.get(),
                              runtime_config);
  reactor.Execute();
  notification.WaitForNotification();

  EXPECT_FALSE(response.response_ciphertext().empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
