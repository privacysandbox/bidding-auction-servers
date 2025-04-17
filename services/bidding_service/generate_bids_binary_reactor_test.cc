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

#include "services/bidding_service/generate_bids_binary_reactor.h"

#include <tuple>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/bidding_service/generate_bids_reactor_test_utils.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using IGForBidding =
    GenerateBidsRequest::GenerateBidsRawRequest::InterestGroupForBidding;
using GenerateBidsRawRequest = GenerateBidsRequest::GenerateBidsRawRequest;
using GenerateBidsRawResponse = GenerateBidsResponse::GenerateBidsRawResponse;
using PABidCallback = absl::AnyInvocable<
    void(
        absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>) &&>;

class GenerateBidsBinaryReactorTest : public testing::Test {
 public:
  GenerateBidByobDispatchClientMock byob_client_;

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
    key_fetcher_manager_ =
        CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
    SetupMockCryptoClientWrapper(*crypto_client_);
    request_.set_key_id(kKeyId);
  }

  void CheckGenerateBids(
      const GenerateBidsRawRequest& raw_request,
      const GenerateBidsRawResponse& expected_raw_response,
      const BiddingServiceRuntimeConfig& runtime_config = {}) {
    GenerateBidsResponse response;
    request_.set_request_ciphertext(raw_request.SerializeAsString());
    grpc::CallbackServerContext context;
    GenerateBidsBinaryReactor reactor(&context, byob_client_, &request_,
                                      &response, key_fetcher_manager_.get(),
                                      crypto_client_.get(), runtime_config);
    reactor.Execute();
    // This check relies on the executor being a mock and executions being
    // single threaded.
    google::protobuf::util::MessageDifferencer diff;
    std::string diff_output;
    diff.ReportDifferencesToString(&diff_output);
    GenerateBidsRawResponse raw_response;
    raw_response.ParseFromString(response.response_ciphertext());
    diff.TreatAsSet(raw_response.GetDescriptor()->FindFieldByName("bids"));
    EXPECT_TRUE(diff.Compare(expected_raw_response, raw_response))
        << diff_output;
  }

  GenerateBidsRequest request_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<MockCryptoClientWrapper> crypto_client_ =
      std::make_unique<MockCryptoClientWrapper>();
};

void CheckRepeatedPtrFieldsEqual(
    const google::protobuf::RepeatedPtrField<std::string>& first,
    const google::protobuf::RepeatedPtrField<std::string>& second) {
  EXPECT_EQ(first.size(), second.size());
  for (auto& first_value : first) {
    EXPECT_THAT(second, testing::Contains(first_value));
  }
}

void CheckBasicFieldsEqual(
    const roma_service::GenerateProtectedAudienceBidRequest& request,
    const GenerateBidsRawRequest& raw_request,
    const IGForBidding& ig_for_bidding) {
  EXPECT_EQ(request.interest_group().name(), ig_for_bidding.name());
  CheckRepeatedPtrFieldsEqual(
      request.interest_group().trusted_bidding_signals_keys(),
      ig_for_bidding.trusted_bidding_signals_keys());
  CheckRepeatedPtrFieldsEqual(request.interest_group().ad_render_ids(),
                              ig_for_bidding.ad_render_ids());
  CheckRepeatedPtrFieldsEqual(
      request.interest_group().ad_component_render_ids(),
      ig_for_bidding.ad_component_render_ids());
  EXPECT_EQ(request.interest_group().user_bidding_signals(),
            ig_for_bidding.user_bidding_signals());
  EXPECT_EQ(request.auction_signals(), raw_request.auction_signals());
  EXPECT_EQ(request.per_buyer_signals(), raw_request.buyer_signals());
  EXPECT_EQ(request.trusted_bidding_signals(),
            ig_for_bidding.trusted_bidding_signals());
  EXPECT_TRUE(request.has_server_metadata());
  if (!raw_request.enable_debug_reporting()) {
    EXPECT_FALSE(request.server_metadata().debug_reporting_enabled());
  }
  EXPECT_EQ(request.server_metadata().logging_enabled(),
            raw_request.consented_debug_config().is_consented() ||
                !server_common::log::IsProd());
}

struct TestDataConfig {
  bool debug_reporting_enabled = false;
  bool logging_enabled = false;
  bool allow_component_auction = false;
  int number_of_bids = 1;
};

std::tuple<IGForBidding, std::vector<AdWithBid>>
GetRandomIGAndAdWithBidsForSingleIG(
    roma_service::GenerateProtectedAudienceBidResponse* bid_response,
    const TestDataConfig& config = {}) {
  IGForBidding interest_group = MakeARandomInterestGroupForBiddingFromBrowser();
  if (config.logging_enabled) {
    bid_response->mutable_log_messages()->add_logs("Log");
    bid_response->mutable_log_messages()->add_warnings("Warning");
    bid_response->mutable_log_messages()->add_errors("Error");
  }
  std::vector<AdWithBid> expected_bids;

  for (int i = 0; i < config.number_of_bids; ++i) {
    int64_t seed = ToUnixNanos(absl::Now());
    *bid_response->add_bids() = MakeARandomRomaProtectedAudienceBid(
        seed, config.debug_reporting_enabled, config.allow_component_auction);
    AdWithBid expected_bid = MakeARandomAdWithBid(
        seed, config.debug_reporting_enabled, config.allow_component_auction);
    expected_bid.set_interest_group_name(interest_group.name());
    // Not supported right now
    expected_bid.clear_private_aggregation_contributions();
    expected_bids.push_back(expected_bid);
  }

  return std::make_tuple(interest_group, expected_bids);
}

TEST_F(GenerateBidsBinaryReactorTest, DoesNotFailDespiteNoIGs) {
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {}});
  ASSERT_EQ(raw_request.interest_group_for_bidding_size(), 0);

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute).Times(0);

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, DoesNotFailOnErrorResponse) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [](const roma_service::GenerateProtectedAudienceBidRequest& request,
             absl::Duration timeout, PABidCallback callback) {
            return absl::UnknownError("Binary responded with not OK status.");
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, DoesNotFailOnErrorResponseInCallback) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [](const roma_service::GenerateProtectedAudienceBidRequest& request,
             absl::Duration timeout, PABidCallback callback) {
            std::move(callback)(
                absl::UnknownError("Binary executed with not OK status."));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, DoesNotFailDespiteUninitializedResponse) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [](const roma_service::GenerateProtectedAudienceBidRequest& request,
             absl::Duration timeout, PABidCallback callback) {
            absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>
                response;
            std::move(callback)(response);
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, DoesNotFailDespiteNoBids) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;

  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  bid_response.mutable_log_messages()->add_logs(
      "This is just to initialize bid_response.");
  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, CreatesRequestForBrowser) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  ig_for_bidding.mutable_browser_signals()->clear_recency_ms();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_EQ(raw_request.interest_group_for_bidding_size(), 1);
  ASSERT_TRUE(raw_request.interest_group_for_bidding(0).has_browser_signals());
  ASSERT_FALSE(raw_request.interest_group_for_bidding(0)
                   .browser_signals()
                   .has_recency_ms());
  ASSERT_TRUE(raw_request.top_level_seller().empty());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_browser_signals());
            EXPECT_EQ(request.browser_signals().top_window_hostname(),
                      raw_request.publisher_name());
            EXPECT_EQ(request.browser_signals().multi_bid_limit(),
                      kDefaultMultiBidLimit);
            EXPECT_EQ(request.browser_signals().seller(), raw_request.seller());
            EXPECT_TRUE(request.browser_signals().top_level_seller().empty());
            EXPECT_EQ(
                request.browser_signals().join_count(),
                ig_for_bidding.browser_signals_for_bidding().join_count());
            EXPECT_EQ(request.browser_signals().bid_count(),
                      ig_for_bidding.browser_signals_for_bidding().bid_count());
            EXPECT_EQ(
                request.browser_signals().recency(),
                ig_for_bidding.browser_signals_for_bidding().recency() * 1000);
            EXPECT_EQ(request.browser_signals().prev_wins(),
                      ig_for_bidding.browser_signals_for_bidding().prev_wins());
            EXPECT_EQ(
                request.browser_signals().prev_wins_ms(),
                ig_for_bidding.browser_signals_for_bidding().prev_wins_ms());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, CreatesRequestForBrowserWithRecencyMs) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_EQ(raw_request.interest_group_for_bidding_size(), 1);
  ASSERT_TRUE(raw_request.interest_group_for_bidding(0)
                  .has_browser_signals_for_bidding());
  ASSERT_TRUE(raw_request.interest_group_for_bidding(0)
                  .browser_signals_for_bidding()
                  .has_recency_ms());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_browser_signals());
            EXPECT_EQ(
                request.browser_signals().recency(),
                ig_for_bidding.browser_signals_for_bidding().recency_ms());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest,
       CreatesRequestForBrowserWithForDebuggingOnlyFlags) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true,
                                   .enable_sampled_debug_reporting = true,
                                   .in_cooldown_or_lockout = true});
  ASSERT_TRUE(raw_request.fdo_flags().in_cooldown_or_lockout());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_browser_signals());
            EXPECT_EQ(request.browser_signals()
                          .for_debugging_only_in_cooldown_or_lockout(),
                      raw_request.fdo_flags().in_cooldown_or_lockout());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest,
       CreatesRequestForBrowserWithMultiBidLimit) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request = BuildGenerateBidsRawRequest(
      {.interest_groups_to_add = {ig_for_bidding}, .multi_bid_limit = 3});
  ASSERT_EQ(raw_request.multi_bid_limit(), 3);

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_browser_signals());
            EXPECT_EQ(request.browser_signals().multi_bid_limit(), 3);
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest,
       CreatesRequestForBrowserWithTopLevelSellerForComponentAuction) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequestForComponentAuction(
          {.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_FALSE(raw_request.top_level_seller().empty());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_browser_signals());
            EXPECT_EQ(request.browser_signals().top_level_seller(),
                      raw_request.top_level_seller());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, CreatesRequestForAndroid) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromAndroid();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_adtech_code_logging = true});
  ASSERT_EQ(raw_request.interest_group_for_bidding_size(), 1);
  ASSERT_TRUE(raw_request.interest_group_for_bidding(0)
                  .has_android_signals_for_bidding());
  ASSERT_TRUE(raw_request.top_level_seller().empty());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_android_signals());
            EXPECT_TRUE(request.android_signals().top_level_seller().empty());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });
  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest,
       CreatesRequestForAndroidWithTopLevelSellerForComponentAuction) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromAndroid();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequestForComponentAuction(
          {.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_EQ(raw_request.interest_group_for_bidding_size(), 1);
  ASSERT_TRUE(raw_request.interest_group_for_bidding(0)
                  .has_android_signals_for_bidding());
  ASSERT_FALSE(raw_request.top_level_seller().empty());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&ig_for_bidding, &raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            CheckBasicFieldsEqual(request, raw_request, ig_for_bidding);
            EXPECT_TRUE(request.has_android_signals());
            EXPECT_EQ(request.android_signals().top_level_seller(),
                      raw_request.top_level_seller());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });
  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, LoggingIsEnabledForConsentedDebug) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_adtech_code_logging = true});
  ASSERT_TRUE(raw_request.consented_debug_config().is_consented());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [&raw_request](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            EXPECT_EQ(request.server_metadata().logging_enabled(),
                      raw_request.consented_debug_config().is_consented());
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });
  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, GeneratesBidForSingleIG) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response);
  ASSERT_EQ(expected_bids.size(), 1);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = std::move(expected_bids[0]);

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, GeneratesMultipleBidsForSingleIG) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response, {.number_of_bids = 2});
  ASSERT_EQ(expected_bids.size(), 2);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_GT(raw_request.multi_bid_limit(), 1);

  GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = std::move(expected_bids[0]);
  *expected_raw_response.add_bids() = std::move(expected_bids[1]);

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesMultipleBidsForMultipleIGsAndEnforcesMultiBidLimit) {
  // IG 1 receives one bid.
  roma_service::GenerateProtectedAudienceBidResponse bid_response_1;
  auto [ig_for_bidding_1, expected_bids_1] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response_1);
  ASSERT_EQ(expected_bids_1.size(), 1);

  // IG 2 receives two bids.
  roma_service::GenerateProtectedAudienceBidResponse bid_response_2;
  auto [ig_for_bidding_2, expected_bids_2] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response_2,
                                          {.number_of_bids = 2});
  ASSERT_EQ(expected_bids_2.size(), 2);

  // IG 3 receives more bids than the limit, so all are dropped.
  roma_service::GenerateProtectedAudienceBidResponse bid_response_3;
  auto [ig_for_bidding_3, _] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response_3, {.number_of_bids = kDefaultMultiBidLimit + 1});

  GenerateBidsRawRequest raw_request = BuildGenerateBidsRawRequest(
      {.interest_groups_to_add = {ig_for_bidding_1, ig_for_bidding_2,
                                  ig_for_bidding_3}});
  ASSERT_GT(raw_request.multi_bid_limit(), 1);

  GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = std::move(expected_bids_1[0]);
  *expected_raw_response.add_bids() = std::move(expected_bids_2[0]);
  *expected_raw_response.add_bids() = std::move(expected_bids_2[1]);

  EXPECT_CALL(byob_client_, Execute)
      .Times(3)
      .WillOnce(
          [bid_response = std::move(bid_response_1)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          })
      .WillOnce(
          [bid_response = std::move(bid_response_2)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          })
      .WillOnce(
          [bid_response = std::move(bid_response_3)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });
  CheckGenerateBids(raw_request, expected_raw_response);
}

// TODO (b/288954720): Once android signals message is defined and signals are
// required, change this test to expect to fail.
TEST_F(GenerateBidsBinaryReactorTest, GeneratesBidsDespiteNoBrowserSignals) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response);
  ig_for_bidding.clear_browser_signals();
  ASSERT_FALSE(ig_for_bidding.has_browser_signals());

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, GeneratesBidsDespiteLoggingEnabled) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.logging_enabled = true});
  ASSERT_GT(bid_response.log_messages().logs_size(), 0);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_adtech_code_logging = true});
  ASSERT_TRUE(raw_request.consented_debug_config().is_consented());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }
  expected_raw_response.set_bidding_export_debug(true);

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, FiltersBidsWithZeroBidPrice) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response_1;
  auto [ig_for_bidding_1, _] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response_1);
  ASSERT_EQ(bid_response_1.bids_size(), 1);
  bid_response_1.mutable_bids(0)->set_bid(0.0f);
  roma_service::GenerateProtectedAudienceBidResponse bid_response_2;
  auto [ig_for_bidding_2, expected_bids_2] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response_2);
  ASSERT_EQ(bid_response_2.bids_size(), 1);
  GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = std::move(expected_bids_2[0]);

  GenerateBidsRawRequest raw_request = BuildGenerateBidsRawRequest(
      {.interest_groups_to_add = {ig_for_bidding_1, ig_for_bidding_2}});

  EXPECT_CALL(byob_client_, Execute)
      .Times(2)
      .WillOnce(
          [bid_response = std::move(bid_response_1)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          })
      .WillOnce(
          [bid_response = std::move(bid_response_2)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });
  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, GeneratesBidsForComponentAuction) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.allow_component_auction = true});

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequestForComponentAuction(
          {.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_FALSE(raw_request.top_level_seller().empty());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest, SkipsUnallowedAdForComponentAuction) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, _] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.allow_component_auction = false});

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequestForComponentAuction(
          {.interest_groups_to_add = {ig_for_bidding}});
  ASSERT_FALSE(raw_request.top_level_seller().empty());

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutDebugUrlsWhenDisabledInRequest) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response);
  ASSERT_EQ(bid_response.bids_size(), 1);
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_loss_url(MakeARandomString());  // Should be filtered
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_win_url(MakeARandomString());  // Should be filtered
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_FALSE(expected_bids[0].has_debug_report_urls());

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = false});
  ASSERT_FALSE(raw_request.enable_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutDebugUrlsWhenDisabledOnServer) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response);
  ASSERT_EQ(bid_response.bids_size(), 1);
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_loss_url(MakeARandomString());  // Should be filtered
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_win_url(MakeARandomString());  // Should be filtered
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_FALSE(expected_bids[0].has_debug_report_urls());

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = false});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithDebugUrlsWhenSamplingDisabled) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.debug_reporting_enabled = true});
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_TRUE(expected_bids[0].has_debug_report_urls());
  ASSERT_FALSE(
      expected_bids[0].debug_report_urls().auction_debug_win_url().empty());
  ASSERT_FALSE(
      expected_bids[0].debug_report_urls().auction_debug_loss_url().empty());

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutDebugUrlsExceedingMaxSize) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.debug_reporting_enabled = true});
  ASSERT_EQ(bid_response.bids_size(), 1);
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_loss_url(
          MakeARandomStringOfLength(65538));  // Should be filtered
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_TRUE(expected_bids[0].has_debug_report_urls());
  expected_bids[0].mutable_debug_report_urls()->clear_auction_debug_loss_url();

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutDebugUrlsExceedingMaxTotalSize) {
  std::vector<InterestGroupForBidding> igs_for_bidding;
  std::vector<roma_service::GenerateProtectedAudienceBidResponse> bid_responses;
  GenerateBidsRawResponse expected_raw_response;
  for (int i = 0; i < 10; ++i) {
    roma_service::GenerateProtectedAudienceBidResponse bid_response;
    auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
        &bid_response, {.debug_reporting_enabled = true});
    igs_for_bidding.push_back(ig_for_bidding);
    ASSERT_TRUE(expected_bids[0].has_debug_report_urls());
    auto mutable_expected_debug_report_urls =
        expected_bids[0].mutable_debug_report_urls();
    auto mutable_response_debug_report_urls =
        bid_response.mutable_bids(0)->mutable_debug_report_urls();
    if (i < 5) {
      std::string win_url = MakeARandomStringOfLength(100);
      std::string loss_url = MakeARandomStringOfLength(100);
      ASSERT_TRUE(expected_bids[0].has_debug_report_urls());
      mutable_expected_debug_report_urls->set_auction_debug_win_url(win_url);
      mutable_expected_debug_report_urls->set_auction_debug_loss_url(loss_url);
      mutable_response_debug_report_urls->set_auction_debug_win_url(win_url);
      mutable_response_debug_report_urls->set_auction_debug_loss_url(loss_url);
    } else if (i == 5) {
      std::string win_url = MakeARandomStringOfLength(23);
      mutable_expected_debug_report_urls->set_auction_debug_win_url(win_url);
      mutable_expected_debug_report_urls->clear_auction_debug_loss_url();
      mutable_response_debug_report_urls->set_auction_debug_win_url(win_url);
    } else {
      expected_bids[0].clear_debug_report_urls();
    }
    *expected_raw_response.add_bids() = std::move(expected_bids[0]);
    bid_responses.emplace_back(std::move(bid_response));
  }
  ASSERT_EQ(bid_responses.size(), 10);
  ASSERT_EQ(igs_for_bidding.size(), 10);
  ASSERT_EQ(expected_raw_response.bids_size(), 10);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = igs_for_bidding,
                                   .enable_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());

  int i = 0;
  EXPECT_CALL(byob_client_, Execute)
      .WillRepeatedly(
          [&bid_responses, &i](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(bid_responses[i++]);
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true,
                     .max_allowed_size_all_debug_urls_kb = 1});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutDebugUrlsWhenBuyerInCooldownOrLockout) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response);
  ASSERT_EQ(bid_response.bids_size(), 1);
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_loss_url(MakeARandomString());  // Should be filtered
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_win_url(MakeARandomString());  // Should be filtered
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_FALSE(expected_bids[0].has_debug_report_urls());

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true,
                                   .enable_sampled_debug_reporting = true,
                                   .in_cooldown_or_lockout = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());
  ASSERT_TRUE(raw_request.fdo_flags().enable_sampled_debug_reporting());
  ASSERT_TRUE(raw_request.fdo_flags().in_cooldown_or_lockout());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = false});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithSampledAndValidatedDebugUrls) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.debug_reporting_enabled = true});
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_TRUE(expected_bids[0].has_debug_report_urls());
  // Debug win url passes size checks, and is selected during sampling.
  ASSERT_FALSE(
      expected_bids[0].debug_report_urls().auction_debug_win_url().empty());
  expected_bids[0].set_debug_win_url_failed_sampling(false);
  // Debug loss url passes size checks, and is selected during sampling.
  ASSERT_FALSE(
      expected_bids[0].debug_report_urls().auction_debug_loss_url().empty());
  expected_bids[0].set_debug_loss_url_failed_sampling(false);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true,
                                   .enable_sampled_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());
  ASSERT_TRUE(raw_request.fdo_flags().enable_sampled_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true,
                     .debug_reporting_sampling_upper_bound = 1});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutSamplingDebugUrlsExceedingMaxSize) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.debug_reporting_enabled = true});
  ASSERT_EQ(bid_response.bids_size(), 1);
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_loss_url(MakeARandomStringOfLength(65538));
  ASSERT_EQ(expected_bids.size(), 1);
  expected_bids[0].clear_debug_report_urls();
  // Debug win url passes size checks, but is not selected during sampling.
  expected_bids[0].set_debug_win_url_failed_sampling(true);
  // Debug loss url fails size check, and is not considered for sampling.
  expected_bids[0].set_debug_loss_url_failed_sampling(false);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true,
                                   .enable_sampled_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());
  ASSERT_TRUE(raw_request.fdo_flags().enable_sampled_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true,
                     .max_allowed_size_debug_url_bytes = 65536,
                     .debug_reporting_sampling_upper_bound = 0});
}

TEST_F(GenerateBidsBinaryReactorTest,
       GeneratesBidsWithoutSamplingDebugUrlsExceedingMaxTotalSize) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] = GetRandomIGAndAdWithBidsForSingleIG(
      &bid_response, {.debug_reporting_enabled = true});
  ASSERT_EQ(bid_response.bids_size(), 1);
  bid_response.mutable_bids(0)
      ->mutable_debug_report_urls()
      ->set_auction_debug_loss_url(MakeARandomStringOfLength(65538));
  ASSERT_EQ(expected_bids.size(), 1);
  expected_bids[0].clear_debug_report_urls();
  // Debug win url passes size checks, but is not selected during sampling.
  expected_bids[0].set_debug_win_url_failed_sampling(true);
  // Debug loss url fails total size check, and is not considered for sampling.
  expected_bids[0].set_debug_loss_url_failed_sampling(false);

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding},
                                   .enable_debug_reporting = true,
                                   .enable_sampled_debug_reporting = true});
  ASSERT_TRUE(raw_request.enable_debug_reporting());
  ASSERT_TRUE(raw_request.fdo_flags().enable_sampled_debug_reporting());

  GenerateBidsRawResponse expected_raw_response;
  for (AdWithBid& expected_bid : expected_bids) {
    *expected_raw_response.add_bids() = std::move(expected_bid);
  }

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.enable_buyer_debug_url_generation = true,
                     .max_allowed_size_all_debug_urls_kb = 1,
                     .debug_reporting_sampling_upper_bound = 0});
}

TEST_F(GenerateBidsBinaryReactorTest, HandlesInvalidTimeout) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [](const roma_service::GenerateProtectedAudienceBidRequest& request,
             absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.roma_timeout_ms = "invalid"});
}

TEST_F(GenerateBidsBinaryReactorTest, TimeoutIsCorrectlyPassedToByobClient) {
  IGForBidding ig_for_bidding = MakeARandomInterestGroupForBiddingFromBrowser();
  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [](const roma_service::GenerateProtectedAudienceBidRequest& request,
             absl::Duration timeout, PABidCallback callback) mutable {
            EXPECT_EQ(timeout, absl::Milliseconds(2000));
            std::move(callback)(
                roma_service::GenerateProtectedAudienceBidResponse());
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response,
                    {.roma_timeout_ms = "2000ms"});
}

TEST_F(GenerateBidsBinaryReactorTest, GeneratesBidWithReportingIds) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  auto [ig_for_bidding, expected_bids] =
      GetRandomIGAndAdWithBidsForSingleIG(&bid_response);
  ASSERT_EQ(expected_bids.size(), 1);
  ASSERT_TRUE(expected_bids[0].has_buyer_and_seller_reporting_id());
  ASSERT_TRUE(expected_bids[0].has_selected_buyer_and_seller_reporting_id());

  GenerateBidsRawRequest raw_request =
      BuildGenerateBidsRawRequest({.interest_groups_to_add = {ig_for_bidding}});

  GenerateBidsRawResponse expected_raw_response;
  *expected_raw_response.add_bids() = std::move(expected_bids[0]);

  EXPECT_CALL(byob_client_, Execute)
      .WillOnce(
          [bid_response = std::move(bid_response)](
              const roma_service::GenerateProtectedAudienceBidRequest& request,
              absl::Duration timeout, PABidCallback callback) mutable {
            std::move(callback)(std::move(bid_response));
            return absl::OkStatus();
          });

  CheckGenerateBids(raw_request, expected_raw_response);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
