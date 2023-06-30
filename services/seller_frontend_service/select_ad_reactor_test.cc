//   Copyright 2022 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/seller_frontend_service/select_ad_reactor.h"

#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <grpcpp/server.h>

#include <gmock/gmock-matchers.h>
#include <google/protobuf/util/message_differencer.h>
#include <include/gmock/gmock-actions.h>

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "include/gmock/gmock.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/cpp/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::TextFormat;
using ::testing::_;
using ::testing::Return;

using Request = SelectAdRequest;
using Response = SelectAdResponse;
using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;

using GetBidDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>) &&>;
using ScoringSignalsDoneCallback =
    absl::AnyInvocable<void(
                           absl::StatusOr<std::unique_ptr<ScoringSignals>>) &&>;
using ScoreAdsDoneCallback =
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                ScoreAdsResponse::ScoreAdsRawResponse>>) &&>;

using AdScore = ScoreAdsResponse::AdScore;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;

using BuyerHostname = std::string;
using AdUrl = std::string;

GetBidsResponse::GetBidsRawResponse BuildGetBidsResponseWithSingleAd(
    const AdUrl& ad_url,
    absl::optional<std::string> interest_group = absl::nullopt,
    absl::optional<float> bid_value = absl::nullopt,
    const bool enable_event_level_debug_reporting = false) {
  AdWithBid bid =
      BuildNewAdWithBid(ad_url, std::move(interest_group), std::move(bid_value),
                        enable_event_level_debug_reporting);
  GetBidsResponse::GetBidsRawResponse response;
  response.mutable_bids()->Add(std::move(bid));
  return response;
}

class SellerFrontEndServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto [protected_audience_input, request, context] =
        GetSampleSelectAdRequest(SelectAdRequest::BROWSER, kSellerOriginDomain);
    protected_audience_input_ = std::move(protected_audience_input);
    request_ = std::move(request);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(context));
    config_.SetFlagForTest(kTrue, ENABLE_ENCRYPTION);
    EXPECT_CALL(key_fetcher_manager_, GetPrivateKey)
        .Times(testing::AnyNumber())
        .WillRepeatedly(Return(GetPrivateKey()));

    // Initialization for telemetry.
    server_common::metric::ServerConfig config_proto;
    config_proto.set_mode(server_common::metric::ServerConfig::PROD);
    metric::SfeContextMap(
        server_common::metric::BuildDependentConfig(config_proto))
        ->Get(&request_);
  }

  void SetupRequestWithTwoBuyers() {
    protected_audience_input_ = MakeARandomProtectedAudienceInput();
    request_ = MakeARandomSelectAdRequest(kSellerOriginDomain,
                                          protected_audience_input_);
    auto [encrypted_protected_audience_input, encryption_context] =
        GetCborEncodedEncryptedInputAndOhttpContext(protected_audience_input_);
    *request_.mutable_protected_audience_ciphertext() =
        std::move(encrypted_protected_audience_input);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(encryption_context));
  }

  void PopulateProtectedAudienceInputCiphertextOnRequest() {
    auto [encrypted_protected_audience_input, encryption_context] =
        GetCborEncodedEncryptedInputAndOhttpContext(protected_audience_input_);
    *request_.mutable_protected_audience_ciphertext() =
        std::move(encrypted_protected_audience_input);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(encryption_context));
  }

  ProtectedAudienceInput protected_audience_input_;
  Request request_;
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> context_;
  TrustedServersConfigClient config_ = CreateConfig();
  server_common::MockKeyFetcherManager key_fetcher_manager_;
};

TEST_F(SellerFrontEndServiceTest, FetchesBidsFromAllBuyers) {
  SetupRequestWithTwoBuyers();

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);

  int buyer_input_count = protected_audience_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);

  auto SetupMockBuyer = [this](const BuyerInput& buyer_input) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([this, &buyer_input](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_values_request,
                      const RequestMetadata& metadata,
                      GetBidDoneCallback on_done, absl::Duration timeout) {
          google::protobuf::util::MessageDifferencer diff;
          std::string diff_output;
          diff.ReportDifferencesToString(&diff_output);
          EXPECT_TRUE(
              diff.Compare(buyer_input, get_values_request->buyer_input()));
          EXPECT_EQ(request_.auction_config().auction_signals(),
                    get_values_request->auction_signals());
          EXPECT_STREQ(protected_audience_input_.publisher_name().c_str(),
                       get_values_request->publisher_name().c_str());
          EXPECT_FALSE(request_.auction_config().seller().empty());
          EXPECT_STREQ(request_.auction_config().seller().c_str(),
                       get_values_request->seller().c_str());
          return absl::OkStatus();
        });
    return buyer;
  };
  ErrorAccumulator error_accumulator;
  for (const auto& buyer_ig_owner : request_.auction_config().buyer_list()) {
    auto decoded_buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        protected_audience_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    const BuyerInput& buyer_input = std::move(decoded_buyer_input);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([SetupMockBuyer, buyer_input](absl::string_view hostname) {
          return SetupMockBuyer(buyer_input);
        });
  }

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_provider, scoring_client, buyer_clients,
                         key_fetcher_manager_, std::move(async_reporter)};

  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
}

TEST_F(SellerFrontEndServiceTest,
       FetchesBidsFromAllBuyersWithDebugReportingEnabled) {
  SetupRequestWithTwoBuyers();

  // Enable debug reporting for this request_.
  protected_audience_input_.set_enable_debug_reporting(true);
  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);

  int buyer_input_count = protected_audience_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);

  auto SetupMockBuyer = [this](const BuyerInput& buyer_input) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([this, &buyer_input](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_values_request,
                      const RequestMetadata& metadata,
                      GetBidDoneCallback on_done, absl::Duration timeout) {
          google::protobuf::util::MessageDifferencer diff;
          std::string diff_output;
          diff.ReportDifferencesToString(&diff_output);
          EXPECT_TRUE(
              diff.Compare(buyer_input, get_values_request->buyer_input()));
          EXPECT_EQ(protected_audience_input_.enable_debug_reporting(),
                    get_values_request->enable_debug_reporting());
          return absl::OkStatus();
        });
    return buyer;
  };

  ErrorAccumulator error_accumulator;
  for (const auto& buyer_ig_owner : request_.auction_config().buyer_list()) {
    auto decoded_buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        protected_audience_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    const BuyerInput& buyer_input = std::move(decoded_buyer_input);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([SetupMockBuyer, buyer_input](absl::string_view hostname) {
          return SetupMockBuyer(buyer_input);
        });
  }

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{scoring_provider, scoring_client, buyer_clients,
                         key_fetcher_manager_, std::move(async_reporter)};

  PopulateProtectedAudienceInputCiphertextOnRequest();
  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
}

TEST_F(SellerFrontEndServiceTest,
       FetchesScoringSignalsWithBidResponseAdRenderUrls) {
  SetupRequestWithTwoBuyers();
  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  // Expects no calls because we do not finish fetching the decision logic
  // code blob, even though we fetch bids and signals.
  EXPECT_CALL(scoring_client, ExecuteInternal).Times(0);

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);
  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = protected_audience_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsList expected_buyer_bids;
  for (const auto& [buyer, unused] : protected_audience_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    auto get_bids_response =
        BuildGetBidsResponseWithSingleAd(url, "testIg", 1.9, true);
    SetupBuyerClientMock(buyer, buyer_clients, get_bids_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bids_response));
  }

  // Scoring Signals Provider
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           std::nullopt);

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager_,
                         std::move(async_reporter)};
  PopulateProtectedAudienceInputCiphertextOnRequest();
  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
}

TEST_F(SellerFrontEndServiceTest, ScoresAdsAfterGettingSignals) {
  SetupRequestWithTwoBuyers();
  absl::flat_hash_map<BuyerHostname, AdUrl> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = protected_audience_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  absl::flat_hash_map<AdUrl, AdWithBid> bids;
  BuyerBidsList expected_buyer_bids;
  for (const auto& [buyer, unused] : protected_audience_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signals provider
  std::string ad_render_urls = "test scoring signals";
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);
  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce(
          [select_ad_req = request_,
           protected_audience_input = protected_audience_input_, ad_render_urls,
           bids](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
                 const RequestMetadata& metadata, ScoreAdsDoneCallback on_done,
                 absl::Duration timeout) {
            google::protobuf::util::MessageDifferencer diff;
            std::string diff_output;
            diff.ReportDifferencesToString(&diff_output);

            EXPECT_STREQ(request->publisher_hostname().data(),
                         protected_audience_input.publisher_name().data());
            EXPECT_EQ(request->seller_signals(),
                      select_ad_req.auction_config().seller_signals());
            EXPECT_EQ(request->auction_signals(),
                      select_ad_req.auction_config().auction_signals());
            EXPECT_STREQ(request->scoring_signals().c_str(),
                         ad_render_urls.c_str());
            for (const auto& actual_ad_with_bid_metadata : request->ad_bids()) {
              AdWithBid actual_ad_with_bid_for_test;
              BuildAdWithBidFromAdWithBidMetadata(actual_ad_with_bid_metadata,
                                                  &actual_ad_with_bid_for_test);
              EXPECT_TRUE(
                  diff.Compare(actual_ad_with_bid_for_test,
                               bids.at(actual_ad_with_bid_for_test.render())));
            }
            EXPECT_EQ(diff_output, "");
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager_,
                         std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
}

TEST_F(SellerFrontEndServiceTest, ReturnsWinningAdAfterScoring) {
  std::string decision_logic = "function scoreAds(){}";

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  BuyerBidsList expected_buyer_bids;
  ErrorAccumulator error_accumulator;
  auto decoded_buyer_inputs = DecodeBuyerInputs(
      protected_audience_input_.buyer_input(), error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());
  for (const auto& [buyer, buyerInput] : decoded_buyer_inputs) {
    EXPECT_EQ(buyerInput.interest_groups_size(), 1);
    std::optional<std::string> ig{
        buyerInput.interest_groups().Get(0).SerializeAsString()};
    auto get_bid_response =
        BuildGetBidsResponseWithSingleAd(buyer_to_ad_url.at(buyer), ig);
    EXPECT_FALSE(get_bid_response.bids(0).render().empty());
    EXPECT_EQ(get_bid_response.bids(0).ad_component_render_size(),
              kDefaultNumAdComponents);
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        std::string(buyer),
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(
            get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  std::string ad_render_urls;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::BlockingCounter scoring_done(1);
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([decision_logic, &scoring_done, &winner, this](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    const RequestMetadata& metadata,
                    ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        ScoreAdsResponse::ScoreAdsRawResponse response;
        float i = 1;
        ErrorAccumulator error_accumulator;
        // Last bid wins.
        for (const auto& bid : score_ads_request->ad_bids()) {
          EXPECT_EQ(bid.ad_cost(), kAdCost);
          const std::string encoded_buyer_input =
              protected_audience_input_.buyer_input()
                  .find(bid.interest_group_owner())
                  ->second;
          BuyerInput decoded_buyer_input =
              DecodeBuyerInput(bid.interest_group_owner(), encoded_buyer_input,
                               error_accumulator);
          EXPECT_FALSE(error_accumulator.HasErrors());
          for (const BuyerInput::InterestGroup& interest_group :
               decoded_buyer_input.interest_groups()) {
            if (std::strcmp(interest_group.name().c_str(),
                            bid.interest_group_name().c_str())) {
              EXPECT_EQ(bid.join_count(),
                        interest_group.browser_signals().join_count());
              EXPECT_EQ(bid.recency(),
                        interest_group.browser_signals().recency());
            }
            break;
          }

          EXPECT_EQ(bid.modeling_signals(), kModelingSignals);
          AdScore score;
          EXPECT_FALSE(bid.render().empty());
          score.set_render(bid.render());
          score.mutable_component_renders()->CopyFrom(
              bid.ad_component_render());
          EXPECT_EQ(bid.ad_component_render_size(), kDefaultNumAdComponents);
          score.set_desirability(i++);
          score.set_buyer_bid(i);
          score.set_interest_group_name(bid.interest_group_name());
          *response.mutable_ad_score() = score;
          winner = score;
        }
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(response));
        scoring_done.DecrementCount();
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager_,
                         std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
  scoring_done.Wait();

  AuctionResult auction_result = DecryptBrowserAuctionResult(
      response.auction_result_ciphertext(), *context_);

  EXPECT_EQ(auction_result.score(), winner.desirability());
  EXPECT_EQ(auction_result.ad_component_render_urls_size(), 3);
  EXPECT_EQ(auction_result.ad_component_render_urls().size(),
            winner.mutable_component_renders()->size());
  EXPECT_EQ(auction_result.ad_render_url(), winner.render());
  EXPECT_EQ(auction_result.bid(), winner.buyer_bid());
  EXPECT_EQ(auction_result.interest_group_name(), winner.interest_group_name());
  EXPECT_EQ(auction_result.interest_group_owner(),
            winner.interest_group_owner());
}

TEST_F(SellerFrontEndServiceTest, ReturnsBiddingGroups) {
  // Setup a buyer input with two interest groups that will have non-zero bids
  // from the bidding service, another interest group with 0 bid and the last
  // interest group that is not present in the bidding data returned by the
  // bidding service.
  const std::string expected_ig_1 = "interest_group_1";
  const std::string expected_ig_2 = "interest_group_2";
  const std::string unexpected_ig_1 = "unexpected_interest_group_1";
  const std::string unexpected_ig_2 = "unexpected_interest_group_2";
  const std::string buyer = "ad_tech_A.com";
  BuyerInput buyer_input;
  buyer_input.mutable_interest_groups()->Add()->set_name(expected_ig_1);
  buyer_input.mutable_interest_groups()->Add()->set_name(expected_ig_2);
  buyer_input.mutable_interest_groups()->Add()->set_name(unexpected_ig_1);
  buyer_input.mutable_interest_groups()->Add()->set_name(unexpected_ig_2);

  // Setup a SelectAdRequest with the aforementioned buyer input.
  request_ = SelectAdRequest();
  request_.mutable_auction_config()->set_seller(kSellerOriginDomain);
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(buyer, buyer_input);
  auto encoded_buyer_inputs = GetEncodedBuyerInputMap(buyer_inputs);
  EXPECT_TRUE(encoded_buyer_inputs.ok());
  *protected_audience_input_.mutable_buyer_input() =
      std::move(*encoded_buyer_inputs);
  request_.mutable_auction_config()->set_seller_signals(
      absl::StrCat("{\"seller_signal\": \"", MakeARandomString(), "\"}"));
  request_.mutable_auction_config()->set_auction_signals(
      absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));
  for (const auto& [local_buyer, unused] :
       protected_audience_input_.buyer_input()) {
    *request_.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_audience_input_.set_generation_id(MakeARandomString());
  protected_audience_input_.set_publisher_name(MakeARandomString());
  request_.set_client_type(SelectAdRequest_ClientType_ANDROID);

  // KV Client
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 1);

  int buyer_input_count = protected_audience_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 1);

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);

  BuyerBidsList expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       protected_audience_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid1 = BuildNewAdWithBid(url, std::move(expected_ig_1), 1);
    AdWithBid bid2 = BuildNewAdWithBid(url, std::move(expected_ig_2), 10);
    AdWithBid bid3 = BuildNewAdWithBid(url, std::move(unexpected_ig_1), 0);
    GetBidsResponse::GetBidsRawResponse response;
    auto mutable_bids = response.mutable_bids();
    mutable_bids->Add(std::move(bid1));
    mutable_bids->Add(std::move(bid2));
    mutable_bids->Add(std::move(bid3));

    SetupBuyerClientMock(local_buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signals provider
  std::string ad_render_urls = "test scoring signals";
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
                   const RequestMetadata& metadata,
                   ScoreAdsDoneCallback on_done, absl::Duration timeout) {
        for (const auto& bid : request->ad_bids()) {
          auto response =
              std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
          AdScore* score = response->mutable_ad_score();
          EXPECT_FALSE(bid.render().empty());
          score->set_render(bid.render());
          score->mutable_component_renders()->CopyFrom(
              bid.ad_component_render());
          EXPECT_EQ(bid.ad_component_render_size(), kDefaultNumAdComponents);
          score->set_desirability(kNonZeroDesirability);
          score->set_buyer_bid(1);
          score->set_interest_group_name(bid.interest_group_name());
          std::move(on_done)(std::move(response));
          // Expect only one bid.
          break;
        }
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager_,
                         std::move(async_reporter)};
  auto [encrypted_protected_audience_input, encrypted_context] =
      GetCborEncodedEncryptedInputAndOhttpContext(protected_audience_input_);
  *request_.mutable_protected_audience_ciphertext() =
      std::move(encrypted_protected_audience_input);
  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      response.auction_result_ciphertext(), encrypted_context);

  // It would have been nice to use ::testing::EqualsProto here but we need to
  // figure out how to set that import via bazel/build.
  EXPECT_EQ(auction_result.bidding_groups().size(), 1);
  const auto& [observed_buyer, interest_groups] =
      *auction_result.bidding_groups().begin();
  EXPECT_EQ(observed_buyer, buyer);
  std::set<int> observed_interest_group_indices(interest_groups.index().begin(),
                                                interest_groups.index().end());
  // We expect indices of interest groups that are:
  // 1. Present in both the returned bids from budding service as well as
  // present
  //  in the initial buyer input.
  // 2. Have non-zero bids in the response returned from bidding service.
  std::set<int> expected_interest_group_indices = {0, 1};
  std::set<int> unexpected_interest_group_indices;
  absl::c_set_difference(
      observed_interest_group_indices, expected_interest_group_indices,
      std::inserter(unexpected_interest_group_indices,
                    unexpected_interest_group_indices.begin()));
  EXPECT_TRUE(unexpected_interest_group_indices.empty());
}

TEST_F(SellerFrontEndServiceTest, PerformsDebugReportingAfterScoring) {
  SetupRequestWithTwoBuyers();
  std::string decision_logic = "function scoreAds(){}";
  // Enable Event Level debug reporting for this request.
  protected_audience_input_.set_enable_debug_reporting(true);

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);
  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = protected_audience_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsList expected_buyer_bids;
  for (const auto& [buyer, unused] : protected_audience_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), "testIg", 1.9, true);
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<BuyerBidsList, ScoringSignals> scoring_signals_provider;
  std::string ad_render_urls;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::BlockingCounter scoring_done(1);
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [decision_logic, &scoring_done, &winner](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              const RequestMetadata& metadata, ScoreAdsDoneCallback on_done,
              absl::Duration timeout) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            float i = 1;
            // Last bid wins.
            for (const auto& bid : request->ad_bids()) {
              EXPECT_EQ(bid.ad_cost(), kAdCost);
              EXPECT_EQ(bid.modeling_signals(), kModelingSignals);
              AdScore score;
              EXPECT_FALSE(bid.render().empty());
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(
                  bid.ad_component_render());
              EXPECT_EQ(bid.ad_component_render_size(),
                        kDefaultNumAdComponents);
              score.set_desirability(i++);
              score.set_buyer_bid(i);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              winner = score;
            }
            std::move(on_done)(
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(
                    response));
            scoring_done.DecrementCount();
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{scoring_signals_provider, scoring_client,
                         buyer_clients, key_fetcher_manager_,
                         std::move(async_reporter)};

  Response response =
      RunRequest<SelectAdReactorForWeb>(config_, clients, request_);
  scoring_done.Wait();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
