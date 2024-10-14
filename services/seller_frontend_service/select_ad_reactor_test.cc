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

#include <gmock/gmock-matchers.h>

#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <grpcpp/server.h>

#include <google/protobuf/reflection.h>
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
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr absl::string_view kProtectedAudienceInput =
    "ProtectedAudienceInput";

using ::google::protobuf::TextFormat;
using ::testing::_;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;

using Request = SelectAdRequest;
using Response = SelectAdResponse;
using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;

using GetBidDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<GetBidsResponse::GetBidsRawResponse>>,
         ResponseMetadata) &&>;
using ScoringSignalsDoneCallback =
    absl::AnyInvocable<void(
                           absl::StatusOr<std::unique_ptr<ScoringSignals>>) &&>;
using ScoreAdsDoneCallback = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>,
         ResponseMetadata) &&>;

using AdScore = ScoreAdsResponse::AdScore;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;

using BuyerHostname = std::string;
using AdUrl = std::string;

template <typename T>
class SellerFrontEndServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto [protected_auction_input, request, context] =
        GetSampleSelectAdRequest<T>(CLIENT_TYPE_BROWSER, kSellerOriginDomain);
    protected_auction_input_ = std::move(protected_auction_input);
    request_ = std::move(request);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(context));
    config_.SetOverride("", CONSENTED_DEBUG_TOKEN);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
    config_.SetOverride(kFalse, ENABLE_CHAFFING);
    EXPECT_CALL(key_fetcher_manager_, GetPrivateKey)
        .Times(testing::AnyNumber())
        .WillRepeatedly(
            [](const google::scp::cpio::PublicPrivateKeyPairId& key_id) {
              EXPECT_EQ(key_id, std::to_string(HpkeKeyset{}.key_id));
              return GetPrivateKey();
            });

    // Initialization for telemetry.
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request_);
  }

  void SetupRequest(int num_buyers, bool set_buyer_egid = false,
                    bool set_seller_egid = false,
                    absl::string_view seller_currency = "",
                    absl::string_view buyer_currency = "",
                    int num_k_anon_ghost_winners = 1,
                    bool enforce_kanon = false) {
    protected_auction_input_ = MakeARandomProtectedAuctionInput<T>(num_buyers);
    protected_auction_input_.set_num_k_anon_ghost_winners(
        num_k_anon_ghost_winners);
    protected_auction_input_.set_enforce_kanon(enforce_kanon);

    request_ = MakeARandomSelectAdRequest(
        kSellerOriginDomain, protected_auction_input_, set_buyer_egid,
        set_seller_egid, seller_currency, buyer_currency);
    auto [encrypted_protected_auction_input, encryption_context] =
        GetCborEncodedEncryptedInputAndOhttpContext<T>(
            protected_auction_input_);
    SetProtectedAuctionCipherText(encrypted_protected_auction_input);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(encryption_context));
  }

  void SetProtectedAuctionCipherText(const std::string& ciphertext) {
    const auto* descriptor = protected_auction_input_.GetDescriptor();
    if (descriptor->name() == kProtectedAudienceInput) {
      *request_.mutable_protected_audience_ciphertext() = ciphertext;
    } else {
      *request_.mutable_protected_auction_ciphertext() = ciphertext;
    }
  }

  void PopulateProtectedAudienceInputCiphertextOnRequest() {
    auto [encrypted_protected_auction_input, encryption_context] =
        GetCborEncodedEncryptedInputAndOhttpContext<T>(
            protected_auction_input_);
    SetProtectedAuctionCipherText(encrypted_protected_auction_input);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(encryption_context));
  }

  T protected_auction_input_;
  Request request_;
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> context_;
  TrustedServersConfigClient config_ = CreateConfig();
  server_common::MockKeyFetcherManager key_fetcher_manager_;
};

using ProtectedAuctionInputTypes =
    ::testing::Types<ProtectedAudienceInput, ProtectedAuctionInput>;
TYPED_TEST_SUITE(SellerFrontEndServiceTest, ProtectedAuctionInputTypes);

TYPED_TEST(SellerFrontEndServiceTest, FetchesBidsFromAllBuyers) {
  this->SetupRequest(/*num_buyers=*/2, /*set_buyer_egid=*/true);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);

  auto SetupMockBuyer = [this](const BuyerInput& buyer_input,
                               absl::string_view buyer_ig_owner) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([this, &buyer_input, buyer_ig_owner](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_bids_request,
                      grpc::ClientContext* context, GetBidDoneCallback on_done,
                      absl::Duration timeout, RequestConfig request_config) {
          google::protobuf::util::MessageDifferencer diff;
          std::string diff_output;
          diff.ReportDifferencesToString(&diff_output);
          EXPECT_EQ(get_bids_request->client_type(),
                    this->request_.client_type());
          EXPECT_TRUE(
              diff.Compare(buyer_input, get_bids_request->buyer_input()));
          EXPECT_EQ(this->request_.auction_config().auction_signals(),
                    get_bids_request->auction_signals());
          EXPECT_TRUE(get_bids_request->has_buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->has_buyer_kv_experiment_group_id(),
                    this->request_.auction_config()
                        .per_buyer_config()
                        .at(buyer_ig_owner)
                        .has_buyer_kv_experiment_group_id());
          EXPECT_GT(get_bids_request->buyer_kv_experiment_group_id(), 0);
          EXPECT_EQ(get_bids_request->buyer_kv_experiment_group_id(),
                    this->request_.auction_config()
                        .per_buyer_config()
                        .at(buyer_ig_owner)
                        .buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->buyer_signals(),
                    this->request_.auction_config()
                        .per_buyer_config()
                        .at(buyer_ig_owner)
                        .buyer_signals());
          EXPECT_EQ(this->protected_auction_input_.publisher_name(),
                    get_bids_request->publisher_name());
          EXPECT_FALSE(this->request_.auction_config().seller().empty());
          EXPECT_EQ(this->request_.auction_config().seller(),
                    get_bids_request->seller());
          EXPECT_EQ(request_config.chaff_request_size, 0);
          std::move(on_done)(
              std::make_unique<GetBidsResponse::GetBidsRawResponse>(), {});
          return absl::OkStatus();
        });
    return buyer;
  };
  ErrorAccumulator error_accumulator;
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  for (const auto& buyer_ig_owner :
       this->request_.auction_config().buyer_list()) {
    auto decoded_buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors())
        << error_accumulator.GetAccumulatedErrorString(
               ErrorVisibility::CLIENT_VISIBLE)
        << "\n\n";
    const BuyerInput& buyer_input = std::move(decoded_buyer_input);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([SetupMockBuyer, buyer_input](absl::string_view hostname) {
          return SetupMockBuyer(buyer_input, hostname);
        });

    entries.emplace_back(buyer_ig_owner,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }

  EXPECT_CALL(buyer_clients, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_provider,
                         scoring_client,
                         buyer_clients,
                         this->key_fetcher_manager_,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
}

TYPED_TEST(SellerFrontEndServiceTest,
           FetchesThreeBidsGivenThreeBuyersWhenLimitUpped) {
  // Should only serve two buyers despite having 3 in request.
  const int num_buyers = 3;
  this->SetupRequest(num_buyers);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);

  auto SetupMockBuyer = [this](const BuyerInput& buyer_input,
                               absl::string_view buyer_ig_owner) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([this, &buyer_input, buyer_ig_owner](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_bids_request,
                      grpc::ClientContext* context, GetBidDoneCallback on_done,
                      absl::Duration timeout, RequestConfig request_config) {
          google::protobuf::util::MessageDifferencer diff;
          std::string diff_output;
          diff.ReportDifferencesToString(&diff_output);
          EXPECT_EQ(get_bids_request->client_type(),
                    this->request_.client_type());
          EXPECT_TRUE(
              diff.Compare(buyer_input, get_bids_request->buyer_input()));
          EXPECT_EQ(this->request_.auction_config().auction_signals(),
                    get_bids_request->auction_signals());
          EXPECT_EQ(this->protected_auction_input_.publisher_name(),
                    get_bids_request->publisher_name());
          EXPECT_FALSE(this->request_.auction_config().seller().empty());

          EXPECT_FALSE(get_bids_request->has_buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->has_buyer_kv_experiment_group_id(),
                    this->request_.auction_config()
                        .per_buyer_config()
                        .at(buyer_ig_owner)
                        .has_buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->buyer_kv_experiment_group_id(), 0);
          EXPECT_EQ(get_bids_request->buyer_kv_experiment_group_id(),
                    this->request_.auction_config()
                        .per_buyer_config()
                        .at(buyer_ig_owner)
                        .buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->buyer_signals(),
                    this->request_.auction_config()
                        .per_buyer_config()
                        .at(buyer_ig_owner)
                        .buyer_signals());
          EXPECT_EQ(this->request_.auction_config().seller(),
                    get_bids_request->seller());
          EXPECT_EQ(request_config.chaff_request_size, 0);
          std::move(on_done)(
              std::make_unique<GetBidsResponse::GetBidsRawResponse>(), {});
          return absl::OkStatus();
        });
    return buyer;
  };
  int num_buyers_solicited = 0;
  ErrorAccumulator error_accumulator;
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  for (const auto& buyer_ig_owner :
       this->request_.auction_config().buyer_list()) {
    auto decoded_buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    const BuyerInput& buyer_input = std::move(decoded_buyer_input);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .Times(::testing::AtMost(1))
        .WillOnce([SetupMockBuyer, buyer_input,
                   &num_buyers_solicited](absl::string_view hostname) {
          ++num_buyers_solicited;
          return SetupMockBuyer(buyer_input, hostname);
        });
    entries.emplace_back(buyer_ig_owner,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }

  EXPECT_CALL(buyer_clients, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_provider,
                         scoring_client,
                         buyer_clients,
                         this->key_fetcher_manager_,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, /*max_buyers_solicited=*/3);
  // Hard limit is calling 3 buyers since we upped it.
  EXPECT_EQ(num_buyers_solicited, 3);
}

TYPED_TEST(SellerFrontEndServiceTest, FetchesTwoBidsGivenThreeBuyers) {
  // Should only serve two buyers despite having 3 in request.
  const int num_buyers = 3;
  this->SetupRequest(num_buyers);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);

  auto SetupMockBuyer = [this](const BuyerInput& buyer_input) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([this, &buyer_input](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_values_request,
                      grpc::ClientContext* context, GetBidDoneCallback on_done,
                      absl::Duration timeout, RequestConfig request_config) {
          google::protobuf::util::MessageDifferencer diff;
          std::string diff_output;
          diff.ReportDifferencesToString(&diff_output);
          EXPECT_EQ(get_values_request->client_type(),
                    this->request_.client_type());
          EXPECT_TRUE(
              diff.Compare(buyer_input, get_values_request->buyer_input()));
          EXPECT_EQ(this->request_.auction_config().auction_signals(),
                    get_values_request->auction_signals());
          EXPECT_EQ(this->protected_auction_input_.publisher_name(),
                    get_values_request->publisher_name());
          EXPECT_FALSE(this->request_.auction_config().seller().empty());
          EXPECT_EQ(this->request_.auction_config().seller(),
                    get_values_request->seller());
          EXPECT_EQ(request_config.chaff_request_size, 0);
          std::move(on_done)(
              std::make_unique<GetBidsResponse::GetBidsRawResponse>(), {});
          return absl::OkStatus();
        });
    return buyer;
  };
  int num_buyers_solicited = 0;
  ErrorAccumulator error_accumulator;
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  for (const auto& buyer_ig_owner :
       this->request_.auction_config().buyer_list()) {
    auto decoded_buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    const BuyerInput& buyer_input = std::move(decoded_buyer_input);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .Times(::testing::AtMost(1))
        .WillOnce([SetupMockBuyer, buyer_input,
                   &num_buyers_solicited](absl::string_view hostname) {
          ++num_buyers_solicited;
          return SetupMockBuyer(buyer_input);
        });
    entries.emplace_back(buyer_ig_owner,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }

  EXPECT_CALL(buyer_clients, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{scoring_provider,
                         scoring_client,
                         buyer_clients,
                         this->key_fetcher_manager_,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  // Hard limit is calling 2 buyers.
  EXPECT_EQ(num_buyers_solicited, 2);
}

TYPED_TEST(SellerFrontEndServiceTest,
           FetchesBidsFromAllBuyersWithDebugReportingEnabled) {
  this->SetupRequest(/*num_buyers=*/2);

  // Enable debug reporting for this request_.
  this->protected_auction_input_.set_enable_debug_reporting(true);
  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);

  auto SetupMockBuyer = [this](const BuyerInput& buyer_input) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([this, &buyer_input](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_values_request,
                      grpc::ClientContext* context, GetBidDoneCallback on_done,
                      absl::Duration timeout, RequestConfig request_config) {
          google::protobuf::util::MessageDifferencer diff;
          std::string diff_output;
          diff.ReportDifferencesToString(&diff_output);
          EXPECT_TRUE(
              diff.Compare(buyer_input, get_values_request->buyer_input()));
          EXPECT_EQ(this->protected_auction_input_.enable_debug_reporting(),
                    get_values_request->enable_debug_reporting());
          EXPECT_EQ(request_config.chaff_request_size, 0);
          std::move(on_done)(
              std::make_unique<GetBidsResponse::GetBidsRawResponse>(), {});
          return absl::OkStatus();
        });
    return buyer;
  };

  ErrorAccumulator error_accumulator;
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  for (const auto& buyer_ig_owner :
       this->request_.auction_config().buyer_list()) {
    auto decoded_buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    const BuyerInput& buyer_input = std::move(decoded_buyer_input);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([SetupMockBuyer, buyer_input](absl::string_view hostname) {
          return SetupMockBuyer(buyer_input);
        });
    entries.emplace_back(buyer_ig_owner,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }

  EXPECT_CALL(buyer_clients, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{scoring_provider,
                         scoring_client,
                         buyer_clients,
                         this->key_fetcher_manager_,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  this->PopulateProtectedAudienceInputCiphertextOnRequest();
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
}

/**
 * This test also includes a specified buyer_currency to demonstrate that
 * setting a buyer currency but having no currency on each bid affects
 nothing
 * adversely.
 */
TYPED_TEST(SellerFrontEndServiceTest,
           FetchesScoringSignalsWithBidResponseAdRenderUrls) {
  this->SetupRequest(/*num_buyers=*/2, /*set_buyer_egid=*/false,
                     /*set_seller_egid=*/true,
                     /*seller_currency=*/kUsdIsoCode);
  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  // Expects no calls because we do not finish fetching the decision logic
  // code blob, even though we fetch bids and signals.
  EXPECT_CALL(scoring_client, ExecuteInternal).Times(0);

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);
  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    auto get_bids_response =
        BuildGetBidsResponseWithSingleAd(url, "testIgName", 1.9, true);
    SetupBuyerClientMock(buyer, buyer_clients, get_bids_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bids_response));
  }

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Scoring Signals Provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  SetupScoringProviderMock(
      /*provider=*/scoring_signals_provider,
      /*expected_buyer_bids=*/expected_buyer_bids,
      /*scoring_signals_value=*/"",
      /*repeated_get_allowed=*/false,
      /*server_error_to_return=*/std::nullopt,
      /*expected_num_bids=*/-1,
      /*seller_egid=*/
      absl::StrCat(this->request_.auction_config()
                       .code_experiment_spec()
                       .seller_kv_experiment_group_id()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  this->PopulateProtectedAudienceInputCiphertextOnRequest();
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
}

/**
 * This test also tests that specifying a currency on an AdWithBid, when no
 * buyer or seller currency is specified, breaks nothing.
 */
TYPED_TEST(SellerFrontEndServiceTest, ScoresAdsAfterGettingSignals) {
  absl::SetFlag(&FLAGS_enable_kanon, true);
  this->SetupRequest(/*num_buyers=*/2,
                     /*set_buyer_egid=*/false,
                     /*set_seller_egid=*/false,
                     /*seller_currency=*/"",
                     /*buyer_currency=*/"",
                     /*num_k_anon_ghost_winners=*/10,
                     /*enforce_kanon=*/true);
  absl::flat_hash_map<BuyerHostname, AdUrl> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  absl::flat_hash_map<AdUrl, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  std::string buyer_reporting_id = "testBuyerReportingId";
  std::string buyer_and_seller_reporting_id = "buyerAndSellerReportingId";
  std::string selectable_buyer_and_seller_reporting_id =
      "selectableBuyerAndSellerReportingId";
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    // Set the bid currency on the AdWithBids.
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(
            /*ad_url = */ url,
            /*interest_group_name = */ absl::nullopt,
            /*bid_value = */ absl::nullopt,
            /*enable_event_level_debug_reporting = */ false,
            /*number_ad_component_render_urls = */ kDefaultNumAdComponents,
            /*bid_currency = */ kEurosIsoCode, buyer_reporting_id,
            buyer_and_seller_reporting_id,
            selectable_buyer_and_seller_reporting_id);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Scoring signals provider
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);
  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([select_ad_req = this->request_,
                 protected_auction_input = this->protected_auction_input_,
                 scoring_signals_value, bids, buyer_reporting_id,
                 buyer_and_seller_reporting_id,
                 selectable_buyer_and_seller_reporting_id](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_raw_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        google::protobuf::util::MessageDifferencer diff;
        std::string diff_output;
        diff.ReportDifferencesToString(&diff_output);

        EXPECT_EQ(score_ads_raw_request->publisher_hostname(),
                  protected_auction_input.publisher_name());
        EXPECT_EQ(score_ads_raw_request->seller_signals(),
                  select_ad_req.auction_config().seller_signals());
        EXPECT_EQ(score_ads_raw_request->auction_signals(),
                  select_ad_req.auction_config().auction_signals());
        EXPECT_EQ(score_ads_raw_request->scoring_signals(),
                  scoring_signals_value);
        EXPECT_EQ(score_ads_raw_request->per_buyer_signals().size(), 2);
        // Even though input request specifies 10 k anon ghost winners,
        // we should only see 1 winner count hardcoded for chrome.
        EXPECT_EQ(score_ads_raw_request->num_allowed_ghost_winners(), 1);
        for (const auto& actual_ad_with_bid_metadata :
             score_ads_raw_request->ad_bids()) {
          AdWithBid actual_ad_with_bid_for_test;
          BuildAdWithBidFromAdWithBidMetadata(
              actual_ad_with_bid_metadata, &actual_ad_with_bid_for_test,
              buyer_reporting_id, buyer_and_seller_reporting_id,
              selectable_buyer_and_seller_reporting_id);
          EXPECT_TRUE(
              diff.Compare(actual_ad_with_bid_for_test,
                           bids.at(actual_ad_with_bid_for_test.render())));
          // k-anon status is not implemented yet and if k-anon is enabled and
          // enforced, we default the k-anon status to false for the bid.
          EXPECT_FALSE(actual_ad_with_bid_metadata.k_anon_status());
        }
        EXPECT_EQ(diff_output, "");
        EXPECT_EQ(score_ads_raw_request->seller(),
                  select_ad_req.auction_config().seller());
        EXPECT_EQ(request_config.chaff_request_size, 0);
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
}

TYPED_TEST(SellerFrontEndServiceTest, DoesNotScoreAdsAfterGettingEmptySignals) {
  this->SetupRequest(/*num_buyers=*/2);
  absl::flat_hash_map<BuyerHostname, AdUrl> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  absl::flat_hash_map<AdUrl, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Scoring signals provider
  std::string scoring_signals_value = "";
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);
  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  // Scoring client should NOT be called when scoring signals are empty.
  EXPECT_CALL(scoring_client, ExecuteInternal).Times(0);

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  // Decrypt to examine whether the result really is chaff.
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), *this->context_);
  // Empty signals should mean no call to auction and a chaff response.
  EXPECT_TRUE(auction_result.is_chaff());
}

TYPED_TEST(SellerFrontEndServiceTest, ReturnsWinningAdAfterScoring) {
  std::string decision_logic = "function scoreAds(){}";

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  BuyerBidsResponseMap expected_buyer_bids;
  ErrorAccumulator error_accumulator;
  auto decoded_buyer_inputs = DecodeBuyerInputs(
      this->protected_auction_input_.buyer_input(), error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());
  for (const auto& [buyer, buyerInput] : decoded_buyer_inputs) {
    EXPECT_EQ(buyerInput.interest_groups_size(), 1);
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), buyerInput.interest_groups().Get(0).name());
    EXPECT_FALSE(get_bid_response.bids(0).render().empty());
    EXPECT_EQ(get_bid_response.bids(0).ad_components_size(),
              kDefaultNumAdComponents);
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        std::string(buyer),
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(
            get_bid_response));
  }

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::BlockingCounter scoring_done(1);
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([decision_logic, &scoring_done, &winner, this](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        ScoreAdsResponse::ScoreAdsRawResponse response;
        float i = 1;
        ErrorAccumulator error_accumulator;
        // Last bid wins.
        for (const auto& bid : score_ads_request->ad_bids()) {
          EXPECT_EQ(bid.ad_cost(), kAdCost);
          const std::string encoded_buyer_input =
              this->protected_auction_input_.buyer_input()
                  .find(bid.interest_group_owner())
                  ->second;
          BuyerInput decoded_buyer_input =
              DecodeBuyerInput(bid.interest_group_owner(), encoded_buyer_input,
                               error_accumulator);
          EXPECT_FALSE(error_accumulator.HasErrors());
          EXPECT_EQ(decoded_buyer_input.interest_groups_size(), 1);
          for (const BuyerInput::InterestGroup& interest_group :
               decoded_buyer_input.interest_groups()) {
            if (interest_group.name() == bid.interest_group_name()) {
              EXPECT_EQ(bid.join_count(),
                        interest_group.browser_signals().join_count());
              EXPECT_EQ(bid.recency(),
                        static_cast<int>(
                            interest_group.browser_signals().recency_ms() /
                            60000));  // recency in minutes
            }
          }
          EXPECT_EQ(request_config.chaff_request_size, 0);

          EXPECT_EQ(bid.modeling_signals(), kModelingSignals);
          AdScore score;
          EXPECT_FALSE(bid.render().empty());
          score.set_render(bid.render());
          score.mutable_component_renders()->CopyFrom(bid.ad_components());
          EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
          score.set_desirability(i++);
          score.set_buyer_bid(i);
          score.set_interest_group_name(bid.interest_group_name());
          *response.mutable_ad_score() = score;
          winner = score;
        }
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(response),
            {});
        scoring_done.DecrementCount();
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  scoring_done.Wait();

  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), *this->context_);

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

/**
 * Creates AdWithBids with a bid currency, then tests that all
 * AdWithBids with the matching currency are accepted and the
 * rest are rejected for mismatch between bid currency and buyer
 * currency.
 */
TYPED_TEST(SellerFrontEndServiceTest,
           CurrencyCheckingBothAcceptsAndRejectsAdWithBids) {
  std::string decision_logic = "function scoreAds(){}";

  const int num_buyers = 2;
  const int num_ad_with_bids = 40;
  const int num_mismatched = 2;
  // (Do not modify this line obviously.)
  const int num_matched = num_ad_with_bids - num_mismatched;

  // By setting the buyer_currency to USD, we ensure the bids will undergo
  // currency checking, since they will have currencies set on them below.
  this->SetupRequest(
      /*num_buyers=*/num_buyers,
      /*set_buyer_egid=*/false,
      /*set_seller_egid=*/false,
      /*seller_currency=*/"",
      /*buyer_currency=*/kUsdIsoCode);

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  BuyerBidsResponseMap expected_filtered_buyer_bids_map;
  ErrorAccumulator error_accumulator;
  auto decoded_buyer_inputs = DecodeBuyerInputs(
      this->protected_auction_input_.buyer_input(), error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());
  ASSERT_EQ(decoded_buyer_inputs.size(), num_buyers);
  for (const auto& [buyer, buyerInput] : decoded_buyer_inputs) {
    EXPECT_EQ(buyerInput.interest_groups_size(), 1);
    std::vector<AdWithBid> ads_with_bids = GetAdWithBidsInMultipleCurrencies(
        num_ad_with_bids, num_mismatched,
        /*matching_currency=*/kUsdIsoCode,
        /*mismatching_currency=*/kEurosIsoCode, buyer_to_ad_url.at(buyer),
        buyerInput.interest_groups().Get(0).name());
    std::vector<ProtectedAppSignalsAdWithBid> pas_ads_with_bids =
        GetPASAdWithBidsInMultipleCurrencies(
            num_ad_with_bids, num_mismatched,
            /*matching_currency=*/kUsdIsoCode,
            /*mismatching_currency=*/kEurosIsoCode, buyer_to_ad_url.at(buyer));
    GetBidsResponse::GetBidsRawResponse actual_bids_for_mock_to_return;
    GetBidsResponse::GetBidsRawResponse expected_bids_once_filtered;
    // Add all AwBs so all are returned by mock.
    for (const auto& ad_with_bid : ads_with_bids) {
      actual_bids_for_mock_to_return.mutable_bids()->Add()->CopyFrom(
          ad_with_bid);
    }
    for (const auto& pas_ad_with_bid : pas_ads_with_bids) {
      actual_bids_for_mock_to_return.mutable_protected_app_signals_bids()
          ->Add()
          ->CopyFrom(pas_ad_with_bid);
    }

    // The check function in scoringSignalsMock cares about order.
    // Thus we construct the expected bids list in the order in which it will
    // come from the reactor. If nothing else this demonstrates that the
    // swap-and-delete algorithm works as expected.
    // First for protected audience.
    expected_bids_once_filtered.mutable_bids()->Add()->CopyFrom(
        ads_with_bids[0]);
    expected_bids_once_filtered.mutable_bids()->Add()->CopyFrom(
        ads_with_bids[39]);
    expected_bids_once_filtered.mutable_bids()->Add()->CopyFrom(
        ads_with_bids[2]);
    expected_bids_once_filtered.mutable_bids()->Add()->CopyFrom(
        ads_with_bids[38]);
    for (int i = 4; i < num_matched; i++) {
      expected_bids_once_filtered.mutable_bids()->Add()->CopyFrom(
          ads_with_bids[i]);
    }
    // Then PAS.
    expected_bids_once_filtered.mutable_protected_app_signals_bids()
        ->Add()
        ->CopyFrom(pas_ads_with_bids[0]);
    expected_bids_once_filtered.mutable_protected_app_signals_bids()
        ->Add()
        ->CopyFrom(pas_ads_with_bids[39]);
    expected_bids_once_filtered.mutable_protected_app_signals_bids()
        ->Add()
        ->CopyFrom(pas_ads_with_bids[2]);
    expected_bids_once_filtered.mutable_protected_app_signals_bids()
        ->Add()
        ->CopyFrom(pas_ads_with_bids[38]);
    for (int i = 4; i < num_matched; i++) {
      expected_bids_once_filtered.mutable_protected_app_signals_bids()
          ->Add()
          ->CopyFrom(pas_ads_with_bids[i]);
    }

    ASSERT_EQ(expected_bids_once_filtered.bids_size(), num_matched);
    ASSERT_EQ(expected_bids_once_filtered.protected_app_signals_bids_size(),
              num_matched);
    // Check a few.
    ASSERT_EQ(expected_bids_once_filtered.bids(0).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/0_USD"));
    ASSERT_EQ(expected_bids_once_filtered.bids(1).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/39_USD"));
    ASSERT_EQ(expected_bids_once_filtered.bids(2).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/2_USD"));
    ASSERT_EQ(expected_bids_once_filtered.bids(3).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/38_USD"));
    ASSERT_EQ(expected_bids_once_filtered.bids(4).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/4_USD"));
    ASSERT_EQ(expected_bids_once_filtered.bids(37).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/37_USD"));
    // And for PAS.
    ASSERT_EQ(
        expected_bids_once_filtered.protected_app_signals_bids(0).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/0_USD"));
    ASSERT_EQ(
        expected_bids_once_filtered.protected_app_signals_bids(1).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/39_USD"));
    ASSERT_EQ(
        expected_bids_once_filtered.protected_app_signals_bids(2).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/2_USD"));
    ASSERT_EQ(
        expected_bids_once_filtered.protected_app_signals_bids(3).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/38_USD"));
    ASSERT_EQ(
        expected_bids_once_filtered.protected_app_signals_bids(4).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/4_USD"));
    ASSERT_EQ(
        expected_bids_once_filtered.protected_app_signals_bids(37).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/37_USD"));

    // Check that a few were constructed correctly.
    ASSERT_EQ(actual_bids_for_mock_to_return.bids_size(), num_ad_with_bids);
    ASSERT_EQ(actual_bids_for_mock_to_return.protected_app_signals_bids_size(),
              num_ad_with_bids);
    ASSERT_EQ(actual_bids_for_mock_to_return.bids(0).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/0_USD"));
    ASSERT_EQ(actual_bids_for_mock_to_return.bids(1).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/1_EUR"));
    ASSERT_EQ(actual_bids_for_mock_to_return.bids(2).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/2_USD"));
    ASSERT_EQ(actual_bids_for_mock_to_return.bids(3).render(),
              absl::StrCat(buyer_to_ad_url.at(buyer), "/3_EUR"));
    ASSERT_EQ(
        actual_bids_for_mock_to_return.protected_app_signals_bids(0).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/0_USD"));
    ASSERT_EQ(
        actual_bids_for_mock_to_return.protected_app_signals_bids(1).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/1_EUR"));
    ASSERT_EQ(
        actual_bids_for_mock_to_return.protected_app_signals_bids(2).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/2_USD"));
    ASSERT_EQ(
        actual_bids_for_mock_to_return.protected_app_signals_bids(3).render(),
        absl::StrCat(buyer_to_ad_url.at(buyer), "/3_EUR"));

    SetupBuyerClientMock(buyer, buyer_clients, actual_bids_for_mock_to_return);
    expected_filtered_buyer_bids_map.try_emplace(
        std::string(buyer),
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(
            expected_bids_once_filtered));
  }
  // One entry for each buyer.
  ASSERT_EQ(expected_filtered_buyer_bids_map.size(), num_buyers);

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  // Inside the ScoringProviderMock, the expected_filtered_buyer_bids_map
  // are compared to the actual bids recieved. This checks whether the bids not
  // in USD made it to fetching scoring signals, which they should not have.
  SetupScoringProviderMock(scoring_signals_provider,
                           expected_filtered_buyer_bids_map,
                           scoring_signals_value);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::BlockingCounter scoring_done(1);
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([decision_logic, &scoring_done, &winner, this](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        ScoreAdsResponse::ScoreAdsRawResponse response;
        float i = 1;
        ErrorAccumulator error_accumulator;
        EXPECT_EQ(score_ads_request->ad_bids_size(), num_buyers * num_matched);
        // Last ad_with_bid_metadata wins.
        for (const auto& ad_with_bid_metadata : score_ads_request->ad_bids()) {
          EXPECT_FALSE(ad_with_bid_metadata.render().empty());
          EXPECT_EQ(ad_with_bid_metadata.ad_cost(), kAdCost);
          EXPECT_EQ(ad_with_bid_metadata.bid_currency(), kUsdIsoCode);
          const std::string encoded_buyer_input =
              this->protected_auction_input_.buyer_input()
                  .find(ad_with_bid_metadata.interest_group_owner())
                  ->second;
          BuyerInput decoded_buyer_input =
              DecodeBuyerInput(ad_with_bid_metadata.interest_group_owner(),
                               encoded_buyer_input, error_accumulator);
          EXPECT_FALSE(error_accumulator.HasErrors());
          EXPECT_EQ(decoded_buyer_input.interest_groups_size(), 1);
          for (const BuyerInput::InterestGroup& interest_group :
               decoded_buyer_input.interest_groups()) {
            if (interest_group.name() ==
                ad_with_bid_metadata.interest_group_name()) {
              EXPECT_EQ(ad_with_bid_metadata.join_count(),
                        interest_group.browser_signals().join_count());
              EXPECT_EQ(ad_with_bid_metadata.recency(),
                        interest_group.browser_signals().recency());
            }
          }
          EXPECT_EQ(ad_with_bid_metadata.modeling_signals(), kModelingSignals);
          EXPECT_EQ(request_config.chaff_request_size, 0);

          AdScore score;
          score.set_render(ad_with_bid_metadata.render());
          score.mutable_component_renders()->CopyFrom(
              ad_with_bid_metadata.ad_components());
          EXPECT_EQ(ad_with_bid_metadata.ad_components_size(),
                    kDefaultNumAdComponents);
          score.set_desirability(i++);
          score.set_buyer_bid(i);
          score.set_interest_group_name(
              ad_with_bid_metadata.interest_group_name());
          *response.mutable_ad_score() = score;
          winner = score;
        }
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(response),
            {});
        scoring_done.DecrementCount();
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  // Filtered bids checked above in the scoring signals provider mock.
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  scoring_done.Wait();

  // Decrypt the response.
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), *this->context_);

  EXPECT_EQ(auction_result.score(), winner.desirability());
  EXPECT_EQ(auction_result.ad_component_render_urls_size(), 3);
  EXPECT_EQ(auction_result.ad_component_render_urls().size(),
            winner.mutable_component_renders()->size());
  EXPECT_EQ(auction_result.ad_render_url(), winner.render());
  // No modified bid set, so bid should be empty.
  EXPECT_EQ(auction_result.bid(), winner.buyer_bid());
  EXPECT_EQ(auction_result.interest_group_name(), winner.interest_group_name());
  EXPECT_EQ(auction_result.interest_group_owner(),
            winner.interest_group_owner());
}

TYPED_TEST(SellerFrontEndServiceTest, ReturnsBiddingGroups) {
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
  this->request_ = SelectAdRequest();
  this->request_.mutable_auction_config()->set_seller(kSellerOriginDomain);
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(buyer, buyer_input);
  auto encoded_buyer_inputs = GetEncodedBuyerInputMap(buyer_inputs);
  EXPECT_TRUE(encoded_buyer_inputs.ok());
  *this->protected_auction_input_.mutable_buyer_input() =
      std::move(*encoded_buyer_inputs);
  this->request_.mutable_auction_config()->set_seller_signals(
      absl::StrCat("{\"seller_signal\": \"", MakeARandomString(), "\"}"));
  this->request_.mutable_auction_config()->set_auction_signals(
      absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));
  for (const auto& [local_buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    *this->request_.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  this->protected_auction_input_.set_generation_id(MakeARandomString());
  this->protected_auction_input_.set_publisher_name(MakeARandomString());
  this->request_.set_client_type(ClientType::CLIENT_TYPE_ANDROID);

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals> scoring_provider;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 1);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 1);

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [local_buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid1 = BuildNewAdWithBid(url, expected_ig_1, 1);
    AdWithBid bid2 = BuildNewAdWithBid(url, expected_ig_2, 10);
    AdWithBid bid3 = BuildNewAdWithBid(url, unexpected_ig_1, 0);
    GetBidsResponse::GetBidsRawResponse response;
    auto* mutable_bids = response.mutable_bids();
    mutable_bids->Add(std::move(bid1));
    mutable_bids->Add(std::move(bid2));
    mutable_bids->Add(std::move(bid3));

    SetupBuyerClientMock(local_buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signals provider
  std::string ad_render_urls = "test scoring signals";
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           ad_render_urls);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
                   grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request->ad_bids_size(), 3);
        EXPECT_EQ(request_config.chaff_request_size, 0);
        // We can return only one score as a winner, so we arbitrarily
        // choose the first.
        const auto& winning_ad_with_bid = request->ad_bids(0);
        auto response =
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
        AdScore* score = response->mutable_ad_score();
        EXPECT_FALSE(winning_ad_with_bid.render().empty());
        score->set_render(winning_ad_with_bid.render());
        score->mutable_component_renders()->CopyFrom(
            winning_ad_with_bid.ad_components());
        EXPECT_EQ(winning_ad_with_bid.ad_components_size(),
                  kDefaultNumAdComponents);
        score->set_desirability(kNonZeroDesirability);
        score->set_buyer_bid(1);
        score->set_interest_group_name(
            winning_ad_with_bid.interest_group_name());
        std::move(on_done)(std::move(response), /* response_metadata= */ {});
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  auto [encrypted_protected_auction_input, encrypted_context] =
      GetCborEncodedEncryptedInputAndOhttpContext<TypeParam>(
          this->protected_auction_input_);
  this->SetProtectedAuctionCipherText(encrypted_protected_auction_input);
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  AuctionResult auction_result = DecryptBrowserAuctionResult(
      *response.mutable_auction_result_ciphertext(), encrypted_context);

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

TYPED_TEST(SellerFrontEndServiceTest, PerformsDebugReportingAfterScoring) {
  this->SetupRequest(/*num_buyers=*/2);
  std::string decision_logic = "function scoreAds(){}";
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);
  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), "testIgName", 1.9, true);
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  absl::BlockingCounter reporting_count(2);
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [decision_logic, &scoring_done, &winner](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            float i = 1;
            // Last bid wins.
            for (const auto& bid : request->ad_bids()) {
              EXPECT_EQ(request_config.chaff_request_size, 0);
              EXPECT_EQ(bid.ad_cost(), kAdCost);
              EXPECT_EQ(bid.modeling_signals(), kModelingSignals);
              AdScore score;
              EXPECT_FALSE(bid.render().empty());
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score.set_desirability(i++);
              score.set_buyer_bid(i);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              winner = score;
            }
            std::move(on_done)(
                std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(
                    response),
                {});
            scoring_done.Notify();
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  EXPECT_CALL(*async_reporter, DoReport)
      .Times(2)
      .WillRepeatedly(
          [&reporting_count](
              const HTTPRequest& reporting_request,
              absl::AnyInvocable<void(absl::StatusOr<absl::string_view>)&&>
                  done_callback) {
            EXPECT_FALSE(reporting_request.url.empty());
            reporting_count.DecrementCount();
          });

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};

  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
  reporting_count.Wait();
}

TYPED_TEST(SellerFrontEndServiceTest,
           DoesntPerformDebugReportingAfterScoringFails) {
  this->SetupRequest(/*num_buyers=*/2);
  std::string decision_logic = "function scoreAds(){}";
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);
  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), "testIgName", 1.9, true);
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            EXPECT_EQ(request_config.chaff_request_size, 0);
            std::move(on_done)(
                absl::Status(absl::StatusCode::kInternal, "No Ads found"),
                /* response_metadata= */ {});
            scoring_done.Notify();
            return absl::OkStatus();
          });
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
}

TYPED_TEST(SellerFrontEndServiceTest,
           ForwardsTopLevelSellertoBuyerAndAuctionServer) {
  absl::string_view top_level_seller = "top_level_seller";
  this->SetupRequest(/*num_buyers=*/2);
  this->request_.mutable_auction_config()->set_top_level_seller(
      top_level_seller);
  absl::flat_hash_map<BuyerHostname, AdUrl> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<AdUrl, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response,
                         /*repeated_get_allowed = */ false,
                         /*expect_all_buyers_solicited =*/true,
                         /*num_buyers_solicited =*/nullptr, top_level_seller);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([&scoring_done, &top_level_seller](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.chaff_request_size, 0);
        EXPECT_EQ(top_level_seller, score_ads_request->top_level_seller());
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(),
            /* response_metadata= */ {});
        scoring_done.Notify();
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,      scoring_client,           buyer_clients,
      this->key_fetcher_manager_,
      /* crypto_client = */ nullptr, std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
}

TYPED_TEST(SellerFrontEndServiceTest, PassesFieldsForServerComponentAuction) {
  std::string top_level_seller = "top_level_seller";
  this->SetupRequest(/*num_buyers=*/2);
  this->request_.mutable_auction_config()->set_top_level_seller(
      top_level_seller);
  this->request_.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_GCP);
  absl::flat_hash_map<BuyerHostname, AdUrl> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<AdUrl, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    AdUrl url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response,
                         /*repeated_get_allowed = */ false,
                         /*expect_all_buyers_solicited =*/true,
                         /*num_buyers_solicited =*/nullptr, top_level_seller);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // Scoring signals must be nonzero for scoring to be attempted.
  std::string scoring_signals_value =
      R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
  SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                           scoring_signals_value);

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([&top_level_seller, &scoring_done](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.chaff_request_size, 0);
        EXPECT_EQ(top_level_seller, score_ads_request->top_level_seller());
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(),
            /* response_metadata= */ {});
        scoring_done.Notify();
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  google::cmrt::sdk::public_key_service::v1::PublicKey key;
  key.set_key_id("key_id");
  EXPECT_CALL(this->key_fetcher_manager_, GetPublicKey).WillOnce(Return(key));
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClient(crypto_client);
  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{
      scoring_signals_provider,   scoring_client, buyer_clients,
      this->key_fetcher_manager_, &crypto_client, std::move(async_reporter)};
  Response response =
      RunRequest<SelectAdReactorForWeb>(this->config_, clients, this->request_);
  scoring_done.WaitForNotification();
  ASSERT_FALSE(response.auction_result_ciphertext().empty());
  EXPECT_EQ(response.key_id(), key.key_id());
}

auto EqGetBidsRawRequestUnlimitedEgress(
    const GetBidsRequest::GetBidsRawRequest& raw_request) {
  return Property(&GetBidsRequest::GetBidsRawRequest::enable_unlimited_egress,
                  Eq(raw_request.enable_unlimited_egress()));
}

TYPED_TEST(SellerFrontEndServiceTest, VerifyUnlimitedEgressFlagPropagates) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         *key_fetcher_manager,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  expected_get_bid_request.set_enable_unlimited_egress(true);

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.chaff_request_size, 0);
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(10.0);
        std::move(on_done)(std::move(get_bids_response),
                           /* response_metadata= */ {});
        return absl::OkStatus();
      };
  auto setup_mock_buyer =
      [&expected_get_bid_request,
       &mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer,
                    ExecuteInternal(Pointee(EqGetBidsRawRequestUnlimitedEgress(
                                        expected_get_bid_request)),
                                    _, _, _, _))
            .WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto mock_buyer_factory = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get)
      .WillRepeatedly(mock_buyer_factory);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_front_end_async_client_factory_mock);

  // Set consented debug config that should be propagated to the downstream
  // services.
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<TypeParam>(
          ClientType::CLIENT_TYPE_ANDROID, kSellerOriginDomain,
          /*is_consented_debug=*/false,
          /* top_level_seller=*/"",
          EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED,
          /*enable_unlimited_egress =*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);

  RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request);
}

auto EqGetBidsRawRequestKAnonFields(
    const GetBidsRequest::GetBidsRawRequest& raw_request) {
  return AllOf(Property(&GetBidsRequest::GetBidsRawRequest::enforce_kanon,
                        Eq(raw_request.enforce_kanon())),
               Property(&GetBidsRequest::GetBidsRawRequest::multi_bid_limit,
                        Eq(raw_request.multi_bid_limit())));
}

TYPED_TEST(SellerFrontEndServiceTest, VerifyKAnonFieldsPropagateToBuyers) {
  absl::SetFlag(&FLAGS_enable_kanon, true);
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         *key_fetcher_manager,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  expected_get_bid_request.set_enforce_kanon(true);
  expected_get_bid_request.set_multi_bid_limit(10);

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.chaff_request_size, 0);
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(10.0);
        std::move(on_done)(std::move(get_bids_response),
                           /*response_metadata=*/{});
        return absl::OkStatus();
      };
  auto setup_mock_buyer =
      [&expected_get_bid_request,
       &mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer,
                    ExecuteInternal(Pointee(EqGetBidsRawRequestKAnonFields(
                                        expected_get_bid_request)),
                                    _, _, _, _))
            .WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto mock_buyer_factory = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get)
      .WillRepeatedly(mock_buyer_factory);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_front_end_async_client_factory_mock);

  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<TypeParam>(
          ClientType::CLIENT_TYPE_ANDROID, kSellerOriginDomain,
          /*is_consented_debug=*/false,
          /* top_level_seller=*/"",
          EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED,
          /*enable_unlimited_egress =*/false,
          /*enforce_kanon=*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);
  buyer_config.set_per_buyer_multi_bid_limit(10);

  RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request);
}

TYPED_TEST(SellerFrontEndServiceTest,
           VerifyKAnonFieldsDontPropagateToBuyersWhenFeatDisabled) {
  absl::SetFlag(&FLAGS_enable_kanon, false);
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         *key_fetcher_manager,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_values_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.chaff_request_size, 0);
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(10.0);
        std::move(on_done)(std::move(get_bids_response),
                           /*response_metadata=*/{});
        return absl::OkStatus();
      };

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  expected_get_bid_request.set_enforce_kanon(false);
  expected_get_bid_request.set_multi_bid_limit(0);
  auto setup_mock_buyer =
      [&expected_get_bid_request,
       &mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer,
                    ExecuteInternal(Pointee(EqGetBidsRawRequestKAnonFields(
                                        expected_get_bid_request)),
                                    _, _, _, _))
            .WillRepeatedly(mock_get_bids);
        return buyer;
      };
  auto mock_buyer_factory = [setup_mock_buyer](absl::string_view hostname) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get)
      .WillRepeatedly(mock_buyer_factory);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_front_end_async_client_factory_mock);

  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<TypeParam>(
          ClientType::CLIENT_TYPE_ANDROID, kSellerOriginDomain,
          /*is_consented_debug=*/false,
          /* top_level_seller=*/"",
          EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED,
          /*enable_unlimited_egress =*/false,
          /*enforce_kanon=*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);
  buyer_config.set_per_buyer_multi_bid_limit(10);

  RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request);
}

TYPED_TEST(SellerFrontEndServiceTest, ChaffingEnabled_SendsChaffRequest) {
  this->config_.SetOverride(kTrue, ENABLE_CHAFFING);

  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  const float winning_bid = 999.0F;
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<TypeParam>(
          CLIENT_TYPE_BROWSER, winning_bid, scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          key_fetcher_manager.get(), expected_buyer_bids, kSellerOriginDomain,
          false);

  std::string chaff_buyer_name = "chaff_buyer";
  GetBidsResponse::GetBidsRawResponse response;

  // Set up buyer calls for chaff buyer.
  auto MockGetBids =
      [response](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        // Verify this is a chaff request by validating the is_chaff and
        // generation_id fields.
        EXPECT_TRUE(get_bids_request->is_chaff());
        EXPECT_GT(request_config.chaff_request_size, 0);
        EXPECT_FALSE(get_bids_request->log_context().generation_id().empty());
        ABSL_LOG(INFO) << "Returning mock bids";
        std::move(on_done)(
            std::make_unique<GetBidsResponse::GetBidsRawResponse>(response),
            /* response_metadata= */ {});
        return absl::OkStatus();
      };
  auto SetupMockBuyer =
      [MockGetBids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal)
            .Times(testing::Exactly(1))
            .WillRepeatedly(MockGetBids);
        return buyer;
      };

  auto MockBuyerFactoryCall =
      [SetupMockBuyer](absl::string_view chaff_buyer_name) {
        return SetupMockBuyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
      };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get(chaff_buyer_name))
      .Times(testing::Exactly(1))
      .WillRepeatedly(MockBuyerFactoryCall);

  // Have the Entries() call on the buyer factory return the 'real' and chaff
  // buyer.
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  for (const auto& [buyer, unused] :
       request_with_context.protected_auction_input.buyer_input()) {
    entries.emplace_back(buyer,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }
  entries.emplace_back(chaff_buyer_name,
                       std::make_shared<BuyerFrontEndAsyncClientMock>());
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  // Append the chaff buyer to the auction_config field in the SelectAd
  // request.
  SelectAdRequest select_ad_request =
      std::move(request_with_context.select_ad_request);
  *select_ad_request.mutable_auction_config()->mutable_buyer_list()->Add() =
      chaff_buyer_name;
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients,
                                               select_ad_request);
  EXPECT_FALSE(encrypted_response.auction_result_ciphertext().empty());
  auto decrypted_response = FromObliviousHTTPResponse(
      *encrypted_response.mutable_auction_result_ciphertext(),
      request_with_context.context, kBiddingAuctionOhttpResponseLabel);
  ASSERT_TRUE(decrypted_response.ok()) << decrypted_response.status();

  absl::StatusOr<std::string> decompressed_response =
      UnframeAndDecompressAuctionResult(*decrypted_response);
  ASSERT_TRUE(decompressed_response.ok())
      << decompressed_response.status().message();

  absl::StatusOr<AuctionResult> auction_result =
      CborDecodeAuctionResultToProto(*decompressed_response);
  EXPECT_TRUE(auction_result.ok()) << auction_result.status();
  EXPECT_FALSE(auction_result->is_chaff());
  EXPECT_EQ(auction_result->bid(), winning_bid);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
