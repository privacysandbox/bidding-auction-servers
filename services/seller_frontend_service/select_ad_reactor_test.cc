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
#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "include/gmock/gmock.h"
#include "services/common/chaffing/mock_moving_median_manager.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/common/random/mock_rng.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/core/test/utils/proto_test_utils.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr absl::string_view kProtectedAudienceInput =
    "ProtectedAudienceInput";
inline constexpr int kDefaultNumBuyers = 2;
inline constexpr int kTestHighestScoringOtherBid = 20;

using ::google::protobuf::TextFormat;
using ::google::scp::core::test::EqualsProto;
using ::testing::_;
using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::UnorderedPointwise;

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

template <typename T, bool UseKvV2ForBrowser>
struct TypeDefinitions {
  typedef T InputType;
  static constexpr bool kUseKvV2ForBrowser = UseKvV2ForBrowser;
};

template <typename T>
class SellerFrontEndServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    privacy_sandbox::bidding_auction_servers::CommonTestInit();
    auto [protected_auction_input, request, context] =
        GetSampleSelectAdRequest<typename T::InputType>(CLIENT_TYPE_BROWSER,
                                                        kSellerOriginDomain);
    protected_auction_input_ = std::move(protected_auction_input);
    request_ = std::move(request);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(context));
    config_ = CreateConfig();
    config_.SetOverride("", CONSENTED_DEBUG_TOKEN);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_APP_SIGNALS);
    config_.SetOverride(kTrue, ENABLE_PROTECTED_AUDIENCE);
    config_.SetOverride(kFalse, ENABLE_TKV_V2_BROWSER);
    if (T::kUseKvV2ForBrowser) {
      config_.SetOverride(kTrue, ENABLE_TKV_V2_BROWSER);
    }
    config_.SetOverride(kFalse, ENABLE_CHAFFING);
    config_.SetOverride(kFalse, ENABLE_CHAFFING_V2);
    config_.SetOverride(kFalse, ENABLE_BUYER_CACHING);
    config_.SetOverride("0", DEBUG_SAMPLE_RATE_MICRO);
    config_.SetOverride(kFalse, CONSENT_ALL_REQUESTS);
    config_.SetOverride("", K_ANON_API_KEY);
    config_.SetOverride(kFalse, TEST_MODE);
    config_.SetOverride("dns:///kv-v2-host", TRUSTED_KEY_VALUE_V2_SIGNALS_HOST);
    config_.SetOverride(kSignalsRequired, SCORING_SIGNALS_FETCH_MODE);
    config_.SetOverride("", HEADER_PASSED_TO_BUYER);
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

  struct SetupRequestOptions {
    const int num_buyers = kDefaultNumBuyers;
    const bool set_buyer_egid = false;
    const bool set_seller_egid = false;
    absl::string_view seller_currency = "";
    absl::string_view buyer_currency = "";
    const int num_k_anon_ghost_winners = 1;
    const bool enforce_kanon = false;
    const bool enable_debug_reporting = true;
    const bool enable_sampled_debug_reporting = false;
    const bool seller_in_cooldown_or_lockout = false;
    absl::string_view top_level_seller = "";
    const bool seller_adtech_is_also_a_buyer = false;
  };

  void SetupRequest(
      const SetupRequestOptions& options = SetupRequestOptions{}) {
    protected_auction_input_ =
        MakeARandomProtectedAuctionInput<typename T::InputType>(
            options.num_buyers);
    protected_auction_input_.set_enable_debug_reporting(
        options.enable_debug_reporting);
    protected_auction_input_.mutable_fdo_flags()
        ->set_enable_sampled_debug_reporting(
            options.enable_sampled_debug_reporting);
    protected_auction_input_.mutable_fdo_flags()->set_in_cooldown_or_lockout(
        options.seller_in_cooldown_or_lockout);
    protected_auction_input_.set_num_k_anon_ghost_winners(
        options.num_k_anon_ghost_winners);
    protected_auction_input_.set_enforce_kanon(options.enforce_kanon);
    if (options.seller_adtech_is_also_a_buyer) {
      // We want the seller adtech to also be a buyer in the auction.
      // protected_auction_input_.buyer_input is a <adtech origin, BuyerInput>
      // map. We can replace the adtech origin in the first entry in buyer_input
      // with the seller's origin.
      auto* buyer_input = protected_auction_input_.mutable_buyer_input();
      if (!buyer_input->empty()) {
        auto it = buyer_input->begin();
        std::string value = it->second;
        buyer_input->erase(it);
        (*buyer_input)[kSellerOriginDomain] = std::move(value);
      }
    }

    request_ = MakeARandomSelectAdRequest(
        kSellerOriginDomain, protected_auction_input_, options.set_buyer_egid,
        options.set_seller_egid, options.seller_currency,
        options.buyer_currency);
    request_.mutable_auction_config()->set_top_level_seller(
        options.top_level_seller);
    auto [encrypted_protected_auction_input, encryption_context] =
        GetCborEncodedEncryptedInputAndOhttpContext<typename T::InputType>(
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
        GetCborEncodedEncryptedInputAndOhttpContext<typename T::InputType>(
            protected_auction_input_);
    SetProtectedAuctionCipherText(encrypted_protected_auction_input);
    context_ = std::make_unique<quiche::ObliviousHttpRequest::Context>(
        std::move(encryption_context));
  }

  typename T::InputType protected_auction_input_;
  Request request_;
  std::unique_ptr<quiche::ObliviousHttpRequest::Context> context_;
  TrustedServersConfigClient config_ = TrustedServersConfigClient({});
  server_common::MockKeyFetcherManager key_fetcher_manager_;
  ReportWinMap report_win_map_;
  std::unique_ptr<MockExecutor> executor_ = std::make_unique<MockExecutor>();
};

using ProtectedAuctionInputTypes =
    ::testing::Types<TypeDefinitions<ProtectedAudienceInput, false>,
                     TypeDefinitions<ProtectedAudienceInput, true>,
                     TypeDefinitions<ProtectedAuctionInput, false>,
                     TypeDefinitions<ProtectedAuctionInput, true>>;
TYPED_TEST_SUITE(SellerFrontEndServiceTest, ProtectedAuctionInputTypes);

TYPED_TEST(SellerFrontEndServiceTest, FetchesBidsFromAllBuyers) {
  this->SetupRequest({.set_buyer_egid = true});

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);

  auto setup_mock_buyer = [this](const BuyerInputForBidding& buyer_input,
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
          auto& buyer_config =
              this->request_.auction_config().per_buyer_config().at(
                  buyer_ig_owner);
          EXPECT_EQ(get_bids_request->client_type(),
                    this->request_.client_type());
          EXPECT_TRUE(diff.Compare(
              buyer_input, get_bids_request->buyer_input_for_bidding()));
          BuyerBlobVersions empty_versions;
          EXPECT_FALSE(
              diff.Compare(empty_versions, get_bids_request->blob_versions()));
          EXPECT_TRUE(diff.Compare(buyer_config.blob_versions(),
                                   get_bids_request->blob_versions()));
          EXPECT_EQ(this->request_.auction_config().auction_signals(),
                    get_bids_request->auction_signals());
          EXPECT_TRUE(get_bids_request->has_buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->has_buyer_kv_experiment_group_id(),
                    buyer_config.has_buyer_kv_experiment_group_id());
          EXPECT_GT(get_bids_request->buyer_kv_experiment_group_id(), 0);
          EXPECT_EQ(get_bids_request->buyer_kv_experiment_group_id(),
                    buyer_config.buyer_kv_experiment_group_id());
          EXPECT_EQ(get_bids_request->buyer_signals(),
                    buyer_config.buyer_signals());
          EXPECT_EQ(this->protected_auction_input_.publisher_name(),
                    get_bids_request->publisher_name());
          EXPECT_FALSE(this->request_.auction_config().seller().empty());
          EXPECT_EQ(this->request_.auction_config().seller(),
                    get_bids_request->seller());
          EXPECT_EQ(request_config.minimum_request_size, 0);
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
    auto buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors())
        << error_accumulator.GetAccumulatedErrorString(
               ErrorVisibility::CLIENT_VISIBLE)
        << "\n\n";
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([setup_mock_buyer, buyer_input](absl::string_view hostname) {
          return setup_mock_buyer(buyer_input, hostname);
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
}

TYPED_TEST(SellerFrontEndServiceTest,
           FetchesThreeBidsGivenThreeBuyersWhenLimitUpped) {
  // Should only serve two buyers despite having 3 in request.
  const int num_buyers = 3;
  this->SetupRequest({.num_buyers = num_buyers});

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);

  auto setup_mock_buyer = [this](const BuyerInputForBidding& buyer_input,
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
          EXPECT_TRUE(diff.Compare(
              buyer_input, get_bids_request->buyer_input_for_bidding()));
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
          EXPECT_EQ(request_config.minimum_request_size, 0);
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
    auto buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .Times(::testing::AtMost(1))
        .WillOnce([setup_mock_buyer, buyer_input,
                   &num_buyers_solicited](absl::string_view hostname) {
          ++num_buyers_solicited;
          return setup_mock_buyer(buyer_input, hostname);
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_,
      /*max_buyers_solicited=*/3,
      /*enable_kanon=*/false);
  // Hard limit is calling 3 buyers since we upped it.
  EXPECT_EQ(num_buyers_solicited, 3);
}

TYPED_TEST(SellerFrontEndServiceTest, FetchesTwoBidsGivenThreeBuyers) {
  // Should only serve two buyers despite having 3 in request.
  const int num_buyers = 3;
  this->SetupRequest({.num_buyers = num_buyers});

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, num_buyers);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, num_buyers);

  auto setup_mock_buyer = [this](const BuyerInputForBidding& buyer_input) {
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
          EXPECT_TRUE(diff.Compare(
              buyer_input, get_values_request->buyer_input_for_bidding()));
          EXPECT_EQ(this->request_.auction_config().auction_signals(),
                    get_values_request->auction_signals());
          EXPECT_EQ(this->protected_auction_input_.publisher_name(),
                    get_values_request->publisher_name());
          EXPECT_FALSE(this->request_.auction_config().seller().empty());
          EXPECT_EQ(this->request_.auction_config().seller(),
                    get_values_request->seller());
          EXPECT_EQ(request_config.minimum_request_size, 0);
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
    auto buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .Times(::testing::AtMost(1))
        .WillOnce([setup_mock_buyer, buyer_input,
                   &num_buyers_solicited](absl::string_view hostname) {
          ++num_buyers_solicited;
          return setup_mock_buyer(buyer_input);
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  // Hard limit is calling 2 buyers.
  EXPECT_EQ(num_buyers_solicited, 2);
}

TYPED_TEST(SellerFrontEndServiceTest,
           FetchesBidsFromAllBuyersWithDebugReportingEnabled) {
  this->SetupRequest(
      {.enable_debug_reporting = true, .enable_sampled_debug_reporting = true});

  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // KV Client
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->request_.auction_config().buyer_list_size();
  EXPECT_EQ(client_count, 2);

  int buyer_input_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(buyer_input_count, 2);

  auto setup_mock_buyer = [this](const BuyerInputForBidding& buyer_input) {
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
          EXPECT_TRUE(diff.Compare(
              buyer_input, get_values_request->buyer_input_for_bidding()));
          EXPECT_EQ(this->protected_auction_input_.enable_debug_reporting(),
                    get_values_request->enable_debug_reporting());
          EXPECT_EQ(this->protected_auction_input_.fdo_flags()
                        .enable_sampled_debug_reporting(),
                    get_values_request->enable_sampled_debug_reporting());
          EXPECT_EQ(request_config.minimum_request_size, 0);
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
    auto buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_FALSE(error_accumulator.HasErrors());
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([setup_mock_buyer, buyer_input](absl::string_view hostname) {
          return setup_mock_buyer(buyer_input);
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  this->PopulateProtectedAudienceInputCiphertextOnRequest();
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
}

/**
 * This test also includes a specified buyer_currency to demonstrate that
 * setting a buyer currency but having no currency on each bid affects
 nothing
 * adversely.
 */
TYPED_TEST(SellerFrontEndServiceTest,
           FetchesScoringSignalsWithBidResponseAdRenderUrls) {
  this->SetupRequest({.set_seller_egid = true, .seller_currency = kUsdIsoCode});
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
    absl::string_view url = buyer_to_ad_url.at(buyer);
    auto get_bids_response =
        BuildGetBidsResponseWithSingleAd(url, {.enable_debug_reporting = true});
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
  KVAsyncClientMock kv_async_client;
  auto seller_egid = absl::StrCat(this->request_.auction_config()
                                      .code_experiment_spec()
                                      .seller_kv_experiment_group_id());
  if (TypeParam::kUseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids,
                           {.seller_egid = seller_egid});
  } else {
    SetupScoringProviderMock(
        scoring_signals_provider, expected_buyer_bids,
        {.scoring_signals_value = "", .seller_egid = seller_egid});
  }
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  this->PopulateProtectedAudienceInputCiphertextOnRequest();
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
}

/**
 * This test also tests that specifying a currency on an AdWithBid, when no
 * buyer or seller currency is specified, breaks nothing.
 */
TYPED_TEST(SellerFrontEndServiceTest, ScoresAdsAfterGettingSignals) {
  this->SetupRequest({.num_k_anon_ghost_winners = 10,
                      .enforce_kanon = true,
                      .enable_debug_reporting = true,
                      .enable_sampled_debug_reporting = true,
                      .seller_in_cooldown_or_lockout = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  std::string buyer_reporting_id = "testBuyerReportingId";
  std::string buyer_and_seller_reporting_id = "buyerAndSellerReportingId";
  std::string selected_buyer_and_seller_reporting_id =
      "selectedBuyerAndSellerReportingId";
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    // Set the bid currency on the AdWithBids.
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(
            /*ad_url=*/url,
            {.bid_currency = kEurosIsoCode,
             .buyer_reporting_id = buyer_reporting_id,
             .buyer_and_seller_reporting_id = buyer_and_seller_reporting_id,
             .selected_buyer_and_seller_reporting_id =
                 selected_buyer_and_seller_reporting_id});
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([select_ad_req = this->request_,
                 protected_auction_input = this->protected_auction_input_, bids,
                 buyer_reporting_id, buyer_and_seller_reporting_id,
                 selected_buyer_and_seller_reporting_id](
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
                  TypeParam::kUseKvV2ForBrowser ? kValidScoringSignalsJsonKvV2
                                                : kValidScoringSignalsJson);
        EXPECT_EQ(
            score_ads_raw_request->seller_data_version(),
            TypeParam::kUseKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        EXPECT_EQ(score_ads_raw_request->score_ad_version(),
                  select_ad_req.auction_config()
                      .code_experiment_spec()
                      .score_ad_version());
        EXPECT_EQ(score_ads_raw_request->per_buyer_signals().size(), 2);
        // Even though input request specifies 10 k anon ghost winners,
        // we should only see 1 winner count hardcoded for chrome.
        EXPECT_EQ(score_ads_raw_request->num_allowed_ghost_winners(), 1);
        for (const auto& actual_ad_with_bid_metadata :
             score_ads_raw_request->ad_bids()) {
          AdWithBid actual_ad_with_bid_for_test =
              BuildAdWithBidFromAdWithBidMetadata(
                  actual_ad_with_bid_metadata, buyer_reporting_id,
                  buyer_and_seller_reporting_id,
                  selected_buyer_and_seller_reporting_id);
          EXPECT_TRUE(
              diff.Compare(actual_ad_with_bid_for_test,
                           bids.at(actual_ad_with_bid_for_test.render())));
          // k-anon status is not implemented yet and if k-anon is enabled and
          // enforced, we default the k-anon status to false for the bid.
          EXPECT_FALSE(actual_ad_with_bid_metadata.k_anon_status());
          EXPECT_TRUE(score_ads_raw_request->enable_debug_reporting());
          EXPECT_TRUE(score_ads_raw_request->fdo_flags()
                          .enable_sampled_debug_reporting());
          EXPECT_TRUE(
              score_ads_raw_request->fdo_flags().in_cooldown_or_lockout());
        }
        EXPECT_EQ(diff_output, "");
        EXPECT_EQ(score_ads_raw_request->seller(),
                  select_ad_req.auction_config().seller());
        EXPECT_EQ(request_config.minimum_request_size, 0);
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  EXPECT_CALL(*this->executor_, Run)
      .WillRepeatedly([](absl::AnyInvocable<void()> closure) { closure(); });
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_,
      /*max_buyers_solicited=*/2, /*enable_kanon=*/true);
}

TYPED_TEST(SellerFrontEndServiceTest,
           DoesNotPerformScoringSignalsFetchAndStillScoresAds) {
  // Setting this flag allows scoring to proceed without scoring signals.
  this->config_.SetOverride(kSignalsNotFetched, SCORING_SIGNALS_FETCH_MODE);

  // Generic Buyer Setup.
  this->SetupRequest();
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    SetupBuyerClientMock(
        buyer, buyer_clients,
        BuildGetBidsResponseWithSingleAd(buyer_to_ad_url.at(buyer)));
  }
  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  ScoringAsyncClientMock scoring_client;
  // We expect that the scoring client will still in fact be called, despite no
  // call to the scoring signals provider and therefore no signals.
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                       score_ads_raw_request,
                   grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        // Request should not include scoring signals or anything else that came
        // from the scoring signals provider (seller KV signals fetch).
        EXPECT_TRUE(score_ads_raw_request->scoring_signals().empty());
        EXPECT_EQ(score_ads_raw_request->seller_data_version(), 0);

        // Now just check that the request is otherwise well-formed at a very
        // basic level.
        EXPECT_EQ(score_ads_raw_request->per_buyer_signals().size(),
                  kDefaultNumBuyers);
        EXPECT_GT(score_ads_raw_request->ad_bids().size(), 0);

        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
        return absl::OkStatus();
      });

  // Reporting Client.
  auto async_reporter = std::make_unique<MockAsyncReporter>(
      std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  // kv_async_client will be nullptr only when SCORING_SIGNALS_FETCH_MODE flag
  // is NOT_FETCHED.
  ClientRegistry clients{/*scoring_signals_provider=*/nullptr,
                         scoring_client,
                         buyer_clients,
                         /*kv_async_client=*/nullptr,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
}

TYPED_TEST(SellerFrontEndServiceTest,
           WhenKAnonDisabledAdsAreConsideredKAnonByDefault) {
  bool enable_kanon = false;
  this->SetupRequest({.num_k_anon_ghost_winners = 10, .enforce_kanon = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    // Set the bid currency on the AdWithBids.
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(
            /*ad_url = */ url, {.bid_currency = kEurosIsoCode});
    bids.insert_or_assign(url, response.bids()[0]);
    SetupBuyerClientMock(buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Scoring signals provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([select_ad_req = this->request_,
                 protected_auction_input = this->protected_auction_input_,
                 bids](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                           score_ads_raw_request,
                       grpc::ClientContext* context,
                       ScoreAdsDoneCallback on_done, absl::Duration timeout,
                       RequestConfig request_config) {
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
                  TypeParam::kUseKvV2ForBrowser ? kValidScoringSignalsJsonKvV2
                                                : kValidScoringSignalsJson);
        EXPECT_EQ(
            score_ads_raw_request->seller_data_version(),
            TypeParam::kUseKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        EXPECT_EQ(score_ads_raw_request->per_buyer_signals().size(), 2);
        // No ghost winner allowed since k-anon is not enabled.
        EXPECT_EQ(score_ads_raw_request->num_allowed_ghost_winners(), 0);
        for (const auto& actual_ad_with_bid_metadata :
             score_ads_raw_request->ad_bids()) {
          AdWithBid actual_ad_with_bid_for_test =
              BuildAdWithBidFromAdWithBidMetadata(actual_ad_with_bid_metadata);
          EXPECT_TRUE(
              diff.Compare(actual_ad_with_bid_for_test,
                           bids.at(actual_ad_with_bid_for_test.render())));
          // When k-anon is not being enforced, consider all the bids as k-anon.
          EXPECT_TRUE(actual_ad_with_bid_metadata.k_anon_status());
        }
        EXPECT_EQ(diff_output, "");
        EXPECT_EQ(score_ads_raw_request->seller(),
                  select_ad_req.auction_config().seller());
        EXPECT_EQ(request_config.minimum_request_size, 0);
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(), {});
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_,
      /*max_buyers_solicited=*/2, enable_kanon);
}

TYPED_TEST(SellerFrontEndServiceTest, DoesNotScoreAdsAfterGettingEmptySignals) {
  this->SetupRequest();
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
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
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  if (TypeParam::kUseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids);
  } else {
    SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                             {.scoring_signals_value = ""});
  }
  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  // Scoring client should NOT be called when scoring signals are empty.
  EXPECT_CALL(scoring_client, ExecuteInternal).Times(0);

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  // Decrypt to examine whether the result really is chaff.
  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;
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
  const PrivateAggregateContribution win_object_contribution =
      GetTestContributionWithSignalObjects(EVENT_TYPE_WIN, "");
  const PrivateAggregateContribution loss_object_contribution =
      GetTestContributionWithSignalObjects(EVENT_TYPE_LOSS, "");
  const PrivateAggregateContribution win_int_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  const PrivateAggregateContribution loss_int_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "");
  std::vector<PrivateAggregateContribution> contributions = {
      win_object_contribution, loss_object_contribution, win_int_contribution,
      loss_int_contribution};
  for (const auto& [buyer, buyer_input_for_bidding] : decoded_buyer_inputs) {
    EXPECT_EQ(buyer_input_for_bidding.interest_groups_size(), 1);
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer),
        {.interest_group_name =
             buyer_input_for_bidding.interest_groups().Get(0).name(),
         .contributions = contributions});
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
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

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
        EXPECT_EQ(score_ads_request->scoring_signals(),
                  TypeParam::kUseKvV2ForBrowser ? kValidScoringSignalsJsonKvV2
                                                : kValidScoringSignalsJson);
        EXPECT_EQ(
            score_ads_request->seller_data_version(),
            TypeParam::kUseKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        // Last bid wins.
        for (const auto& bid : score_ads_request->ad_bids()) {
          EXPECT_EQ(bid.ad_cost(), kAdCost);
          const std::string encoded_buyer_input =
              this->protected_auction_input_.buyer_input()
                  .find(bid.interest_group_owner())
                  ->second;
          BuyerInputForBidding decoded_buyer_input =
              DecodeBuyerInput(bid.interest_group_owner(), encoded_buyer_input,
                               error_accumulator);
          EXPECT_FALSE(error_accumulator.HasErrors());
          EXPECT_EQ(decoded_buyer_input.interest_groups_size(), 1);
          for (const BuyerInputForBidding::InterestGroupForBidding&
                   interest_group : decoded_buyer_input.interest_groups()) {
            if (interest_group.name() == bid.interest_group_name()) {
              EXPECT_EQ(bid.join_count(),
                        interest_group.browser_signals().join_count());
              EXPECT_EQ(bid.recency(),
                        static_cast<int>(
                            interest_group.browser_signals().recency_ms() /
                            60000));  // recency in minutes
            }
          }
          EXPECT_EQ(request_config.minimum_request_size, 0);

          EXPECT_EQ(bid.modeling_signals(), kModelingSignals);
          AdScore score;
          EXPECT_FALSE(bid.render().empty());
          score.set_render(bid.render());
          score.mutable_component_renders()->CopyFrom(bid.ad_components());
          EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
          score.set_desirability(i++);
          score.set_buyer_bid(i);
          score.set_interest_group_name(bid.interest_group_name());
          auto* highest_scoring_other_bids_map =
              score.mutable_ig_owner_highest_scoring_other_bids_map();
          highest_scoring_other_bids_map->try_emplace(
              bid.interest_group_owner(), google::protobuf::ListValue());
          highest_scoring_other_bids_map->at(bid.interest_group_owner())
              .add_values()
              ->set_number_value(kTestHighestScoringOtherBid);
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_,
      /*max_buyers_solicited=*/3,
      /*enable_kanon=*/false,
      /*enable_buyer_private_aggregate_reporting=*/true,
      /*per_adtech_paapi_contributions_limit=*/100);
  scoring_done.Wait();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  EXPECT_EQ(auction_result.score(), winner.desirability());
  EXPECT_EQ(auction_result.ad_component_render_urls_size(), 3);
  EXPECT_EQ(auction_result.ad_component_render_urls().size(),
            winner.mutable_component_renders()->size());
  EXPECT_EQ(auction_result.ad_render_url(), winner.render());
  EXPECT_EQ(auction_result.bid(), winner.buyer_bid());
  EXPECT_EQ(auction_result.interest_group_name(), winner.interest_group_name());
  EXPECT_EQ(auction_result.interest_group_owner(),
            winner.interest_group_owner());
  EXPECT_EQ(auction_result.top_level_contributions_size(), 1);
  EXPECT_EQ(auction_result.top_level_contributions(0).contributions_size(), 2);
  BaseValues base_values = {.highest_scoring_other_bid =
                                kTestHighestScoringOtherBid};
  // SignalBucket with BASE_VALUE_HIGHEST_SCORING_OTHER_BID is expected to be
  // converted to Bucket128Bit in the final contribution.
  absl::StatusOr<Bucket128Bit> test_final_bucket_from_signal =
      GetPrivateAggregationBucketPostAuction(base_values,
                                             win_object_contribution.bucket());
  ASSERT_TRUE(test_final_bucket_from_signal.ok())
      << test_final_bucket_from_signal.status();
  google::protobuf::util::MessageDifferencer diff;
  std::string diff_output;
  diff.ReportDifferencesToString(&diff_output);
  EXPECT_TRUE(diff.Compare(*test_final_bucket_from_signal,
                           auction_result.top_level_contributions(0)
                               .contributions(0)
                               .bucket()
                               .bucket_128_bit()))
      << diff_output;
  EXPECT_TRUE(diff.Compare(
      win_int_contribution.bucket(),
      auction_result.top_level_contributions(0).contributions(1).bucket()))
      << diff_output;
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

  const int num_ad_with_bids = 40;
  const int num_mismatched = 2;
  // (Do not modify this line obviously.)
  const int num_matched = num_ad_with_bids - num_mismatched;

  // By setting the buyer_currency to USD, we ensure the bids will undergo
  // currency checking, since they will have currencies set on them below.
  this->SetupRequest({.buyer_currency = kUsdIsoCode});

  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  BuyerBidsResponseMap expected_filtered_buyer_bids_map;
  ErrorAccumulator error_accumulator;
  auto decoded_buyer_inputs = DecodeBuyerInputs(
      this->protected_auction_input_.buyer_input(), error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());
  ASSERT_EQ(decoded_buyer_inputs.size(), kDefaultNumBuyers);
  for (const auto& [buyer, buyer_input] : decoded_buyer_inputs) {
    EXPECT_EQ(buyer_input.interest_groups_size(), 1);
    std::vector<AdWithBid> ads_with_bids = GetAdWithBidsInMultipleCurrencies(
        num_ad_with_bids, num_mismatched,
        /*matching_currency=*/kUsdIsoCode,
        /*mismatching_currency=*/kEurosIsoCode, buyer_to_ad_url.at(buyer),
        buyer_input.interest_groups().Get(0).name());
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
  ASSERT_EQ(expected_filtered_buyer_bids_map.size(), kDefaultNumBuyers);

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  // Inside the ScoringProviderMock, the expected_filtered_buyer_bids_map
  // are compared to the actual bids recieved. This checks whether the bids
  // not in USD made it to fetching scoring signals, which they should not
  // have.
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_filtered_buyer_bids_map));

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
        EXPECT_EQ(score_ads_request->scoring_signals(),
                  TypeParam::kUseKvV2ForBrowser ? kValidScoringSignalsJsonKvV2
                                                : kValidScoringSignalsJson);
        EXPECT_EQ(
            score_ads_request->seller_data_version(),
            TypeParam::kUseKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        EXPECT_EQ(score_ads_request->ad_bids_size(),
                  kDefaultNumBuyers * num_matched);
        // Last ad_with_bid_metadata wins.
        for (const auto& ad_with_bid_metadata : score_ads_request->ad_bids()) {
          EXPECT_FALSE(ad_with_bid_metadata.render().empty());
          EXPECT_EQ(ad_with_bid_metadata.ad_cost(), kAdCost);
          EXPECT_EQ(ad_with_bid_metadata.bid_currency(), kUsdIsoCode);
          const std::string encoded_buyer_input =
              this->protected_auction_input_.buyer_input()
                  .find(ad_with_bid_metadata.interest_group_owner())
                  ->second;
          BuyerInputForBidding decoded_buyer_input =
              DecodeBuyerInput(ad_with_bid_metadata.interest_group_owner(),
                               encoded_buyer_input, error_accumulator);
          EXPECT_FALSE(error_accumulator.HasErrors());
          EXPECT_EQ(decoded_buyer_input.interest_groups_size(), 1);
          for (const BuyerInputForBidding::InterestGroupForBidding&
                   interest_group : decoded_buyer_input.interest_groups()) {
            if (interest_group.name() ==
                ad_with_bid_metadata.interest_group_name()) {
              EXPECT_EQ(ad_with_bid_metadata.join_count(),
                        interest_group.browser_signals().join_count());
              EXPECT_EQ(ad_with_bid_metadata.recency(),
                        interest_group.browser_signals().recency());
            }
          }
          EXPECT_EQ(ad_with_bid_metadata.modeling_signals(), kModelingSignals);
          EXPECT_EQ(request_config.minimum_request_size, 0);

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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  // Filtered bids checked above in the scoring signals provider mock.
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.Wait();

  // Decrypt the response.
  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

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
  // from the bidding service, another interest group with 0 bid and the
  // last interest group that is not present in the bidding data returned by
  // the bidding service.
  const std::string expected_ig_1 = "interest_group_1";
  const std::string expected_ig_2 = "interest_group_2";
  const std::string unexpected_ig_1 = "unexpected_interest_group_1";
  const std::string unexpected_ig_2 = "unexpected_interest_group_2";
  const std::string buyer = "ad_tech_A.com";
  BuyerInputForBidding buyer_input;
  buyer_input.mutable_interest_groups()->Add()->set_name(expected_ig_1);
  buyer_input.mutable_interest_groups()->Add()->set_name(expected_ig_2);
  buyer_input.mutable_interest_groups()->Add()->set_name(unexpected_ig_1);
  buyer_input.mutable_interest_groups()->Add()->set_name(unexpected_ig_2);

  // Setup a SelectAdRequest with the aforementioned buyer input.
  this->request_ = SelectAdRequest();
  this->request_.mutable_auction_config()->set_seller(kSellerOriginDomain);
  google::protobuf::Map<std::string, BuyerInputForBidding> buyer_inputs;
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
  this->request_.set_client_type(ClientType::CLIENT_TYPE_BROWSER);

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
    std::string url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid1 = BuildNewAdWithBid(
        url, {.interest_group_name = expected_ig_1, .bid_value = 1});
    AdWithBid bid2 = BuildNewAdWithBid(
        url, {.interest_group_name = expected_ig_2, .bid_value = 10});
    AdWithBid bid3 = BuildNewAdWithBid(
        url, {.interest_group_name = unexpected_ig_1, .bid_value = 0});
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
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillOnce([](std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
                   grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                   absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request->scoring_signals(), TypeParam::kUseKvV2ForBrowser
                                                  ? kValidScoringSignalsJsonKvV2
                                                  : kValidScoringSignalsJson);
        EXPECT_EQ(
            request->seller_data_version(),
            TypeParam::kUseKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        EXPECT_EQ(request->ad_bids_size(), 3);
        EXPECT_EQ(request_config.minimum_request_size, 0);
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
        std::move(on_done)(std::move(response), /*response_metadata=*/{});
        return absl::OkStatus();
      });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  auto [encrypted_protected_auction_input, encrypted_context] =
      GetCborEncodedEncryptedInputAndOhttpContext<
          typename TypeParam::InputType>(this->protected_auction_input_);
  this->SetProtectedAuctionCipherText(encrypted_protected_auction_input);
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), encrypted_context)
          .first;

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

TYPED_TEST(SellerFrontEndServiceTest,
           ForwardsTopLevelSellerToBuyerAndAuctionServer) {
  this->SetupRequest({.top_level_seller = kTestTopLevelSellerOriginDomain});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([&scoring_done](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(score_ads_request->scoring_signals(),
                  TypeParam::kUseKvV2ForBrowser ? kValidScoringSignalsJsonKvV2
                                                : kValidScoringSignalsJson);
        EXPECT_EQ(
            score_ads_request->seller_data_version(),
            TypeParam::kUseKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        EXPECT_EQ(request_config.minimum_request_size, 0);
        EXPECT_EQ(score_ads_request->top_level_seller(),
                  kTestTopLevelSellerOriginDomain);
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(),
            /*response_metadata=*/{});
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();
}

TYPED_TEST(SellerFrontEndServiceTest, PassesFieldsForServerComponentAuction) {
  this->SetupRequest({.top_level_seller = kTestTopLevelSellerOriginDomain});
  this->request_.mutable_auction_config()->set_top_level_cloud_platform(
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_GCP);
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  bool useKvV2ForBrowser = TypeParam::kUseKvV2ForBrowser;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce([&scoring_done, useKvV2ForBrowser](
                    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest>
                        score_ads_request,
                    grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
                    absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(score_ads_request->scoring_signals(),
                  useKvV2ForBrowser ? kValidScoringSignalsJsonKvV2
                                    : kValidScoringSignalsJson);
        EXPECT_EQ(score_ads_request->seller_data_version(),
                  useKvV2ForBrowser ? 0 : kDefaultSellerDataVersion);
        EXPECT_EQ(request_config.minimum_request_size, 0);
        EXPECT_EQ(score_ads_request->top_level_seller(),
                  kTestTopLevelSellerOriginDomain);
        std::move(on_done)(
            std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(),
            /*response_metadata=*/{});
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         &crypto_client,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();
  ASSERT_FALSE(response.auction_result_ciphertext().empty());
  EXPECT_EQ(response.key_id(), key.key_id());
}

TYPED_TEST(SellerFrontEndServiceTest,
           SendsDebugPingsForSingleSellerAuctionWhenSamplingDisabled) {
  this->SetupRequest();
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
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  absl::BlockingCounter reporting_count(2);
  AdScore winner, loser;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
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
  bool debug_loss_url_pinged;
  bool debug_win_url_pinged;
  EXPECT_CALL(*async_reporter, DoReport)
      .Times(2)
      .WillRepeatedly(
          [&reporting_count, &debug_loss_url_pinged, &loser,
           &debug_win_url_pinged, &winner](
              const HTTPRequest& reporting_request,
              absl::AnyInvocable<void(absl::StatusOr<absl::string_view>)&&>
                  done_callback) {
            if (reporting_request.url ==
                absl::StrCat(kTestBuyerDebugLossUrlPrefix, loser.render())) {
              debug_loss_url_pinged = true;
            } else if (reporting_request.url ==
                       absl::StrCat(kTestBuyerDebugWinUrlPrefix,
                                    winner.render())) {
              debug_win_url_pinged = true;
            }
            reporting_count.DecrementCount();
          });

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();
  reporting_count.Wait();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Debug reports should not be present in auction result since it is not a
  // component auction.
  EXPECT_TRUE(auction_result.adtech_origin_debug_urls_map().empty());
  // Debug pings should be sent from the server for both losing and winning ads.
  EXPECT_TRUE(debug_loss_url_pinged);
  EXPECT_TRUE(debug_win_url_pinged);
}

TYPED_TEST(
    SellerFrontEndServiceTest,
    SendsDebugPingsToLosersAndReturnsDebugReportsForComponentWinnerWhenSamplingDisabled) {  // NOLINT
  this->SetupRequest({.top_level_seller = kTestTopLevelSellerOriginDomain});
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
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  absl::Notification reporting_done;
  AdScore winner, loser;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              // Fields for component auction.
              score.set_ad_metadata(kTestAdMetadata);
              score.set_allow_component_auction(true);
              score.set_bid(i + 1);
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
            }
            *response.mutable_seller_debug_reports() = seller_reports;
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
      .WillOnce(
          [&reporting_done, &loser](
              const HTTPRequest& reporting_request,
              absl::AnyInvocable<void(absl::StatusOr<absl::string_view>)&&>
                  done_callback) {
            // Debug pings should be sent from the server only for the losing
            // ads in component auction.
            EXPECT_EQ(
                reporting_request.url,
                absl::StrCat(kTestBuyerDebugLossUrlPrefix, loser.render()));
            reporting_done.Notify();
          });

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();
  reporting_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Debug pings should not be sent from the server for the winning ad in a
  // component auction. Debug reports should be present in the response so that
  // the client can send the correct debug pings depending on whether this ad
  // wins or loses the top level auction.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(
          absl::StrCat(kTestBuyerDebugWinReportForComponentWinnerTemplate,
                       kTestBuyerDebugLossReportForComponentWinnerTemplate),
          winner.render()),
      &expected_buyer_reports));
  EXPECT_THAT(
      debug_urls_map.at(winner.interest_group_owner()).reports(),
      UnorderedPointwise(EqualsProto(), expected_buyer_reports.reports()));

  // Verify seller debug reports.
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain),
              EqualsProto(seller_reports));
}

TYPED_TEST(SellerFrontEndServiceTest,
           MergesBuyerAndSellerDebugReportsOfSameAdtechWhenSamplingDisabled) {
  this->SetupRequest({.num_buyers = 1,
                      .top_level_seller = kTestTopLevelSellerOriginDomain,
                      .seller_adtech_is_also_a_buyer = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  std::string winner_render;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner_render, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // The bid by the seller adtech (also a buyer in the auction) wins.
            AdScore score;
            const auto& bid = request->ad_bids(0);
            if (bid.interest_group_owner() == kSellerOriginDomain) {
              winner_render = bid.render();
              score.set_desirability(1);
              score.set_buyer_bid(1);
              score.set_bid(1);
            }
            score.set_render(bid.render());
            score.mutable_component_renders()->CopyFrom(bid.ad_components());
            score.set_interest_group_name(bid.interest_group_name());
            score.set_interest_group_owner(bid.interest_group_owner());
            // Fields for component auction.
            score.set_ad_metadata(kTestAdMetadata);
            score.set_allow_component_auction(true);
            *response.mutable_ad_score() = score;
            *response.mutable_seller_debug_reports() = seller_reports;
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

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Verify debug reports for the adtech that is both the seller adtech and a
  // buyer in the auction. All debug reports for the component winner should be
  // present.
  auto& debug_urls_map = auction_result.adtech_origin_debug_urls_map();
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  DebugReports expected_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat(
          absl::Substitute(
              absl::StrCat(kTestBuyerDebugWinReportForComponentWinnerTemplate,
                           kTestBuyerDebugLossReportForComponentWinnerTemplate),
              winner_render),
          kTestSellerDebugReportsForComponentWinner),
      &expected_reports));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain),
              EqualsProto(expected_reports));
}

TYPED_TEST(
    SellerFrontEndServiceTest,
    DoesNotSendDebugPingsOrReturnDebugReportsWhenDebugReportingDisabled) {
  this->SetupRequest({.enable_debug_reporting = false,
                      .top_level_seller = kTestTopLevelSellerOriginDomain});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  absl::Notification reporting_done;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              // Fields for component auction.
              score.set_ad_metadata(kTestAdMetadata);
              score.set_allow_component_auction(true);
              score.set_bid(i + 1);
              *response.mutable_ad_score() = score;
              *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server since debug reporting is not
  // enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Debug reports should not be present in the response since we do not want
  // the client to send any debug pings either.
  EXPECT_TRUE(auction_result.adtech_origin_debug_urls_map().empty());
}

TYPED_TEST(SellerFrontEndServiceTest,
           DoesNotSendDebugPingsOrReturnDebugReportsWhenScoringFails) {
  this->SetupRequest({.top_level_seller = kTestTopLevelSellerOriginDomain});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            std::move(on_done)(
                absl::Status(absl::StatusCode::kInternal, "No Ads found"),
                /*response_metadata=*/{});
            scoring_done.Notify();
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Debug pings should not be sent from the server since ad scoring failed.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Debug reports should not be present in the response since scoring failed.
  EXPECT_TRUE(auction_result.adtech_origin_debug_urls_map().empty());
}

TYPED_TEST(SellerFrontEndServiceTest,
           DoesNotSendSampledDebugPingsOrReturnDebugReportsWhenScoringFails) {
  this->SetupRequest({.enable_sampled_debug_reporting = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  absl::flat_hash_map<std::string, AdWithBid> bids;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    absl::string_view url = buyer_to_ad_url.at(buyer);
    GetBidsResponse::GetBidsRawResponse response =
        BuildGetBidsResponseWithSingleAd(url);
    bids.insert_or_assign(url, response.bids().at(0));
    SetupBuyerClientMock(buyer, buyer_clients, response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            std::move(on_done)(
                absl::Status(absl::StatusCode::kInternal, "No Ads found"),
                /*response_metadata=*/{});
            scoring_done.Notify();
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // Debug pings should not be sent from the server since ad scoring failed.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Debug reports should not be present in the response since scoring failed.
  EXPECT_TRUE(auction_result.adtech_origin_debug_urls_map().empty());
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsMaxOneSampledDebugReportPerAdtechForSingleSellerAuction) {
  this->SetupRequest({.num_buyers = 1, .enable_sampled_debug_reporting = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 1);
  BuyerBidsResponseMap expected_buyer_bids;
  // Single buyer has two ads: one wins and one loses.
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        absl::StrCat(buyer_to_ad_url.at(buyer), "_0"),
        {.interest_group_name = "ig_0", .enable_debug_reporting = true});
    get_bid_response.mutable_bids()->Add(BuildNewAdWithBid(
        absl::StrCat(buyer_to_ad_url.at(buyer), "_1"),
        {.interest_group_name = "ig_1", .enable_debug_reporting = true}));
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner, loser;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugWinReport, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
            }
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_win_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(kTestBuyerDebugWinReportTemplate, winner.render()),
      &expected_buyer_win_report));
  DebugReports expected_buyer_loss_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(kTestBuyerDebugLossReportTemplate, loser.render()),
      &expected_buyer_loss_report));
  // Even though both debug win url for winning ad and debug loss url for losing
  // ad are valid, only one of them is sent back to the client since there is a
  // limit of one debug report per adtech for single-seller auctions.
  EXPECT_THAT(debug_urls_map.at(winner.interest_group_owner()),
              AnyOf(EqualsProto(expected_buyer_win_report),
                    EqualsProto(expected_buyer_loss_report)));

  // Verify seller debug reports.
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain),
              EqualsProto(seller_reports));
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsSampledDebugReportsForAllBuyersInSingleSellerAuction) {
  this->SetupRequest({.enable_sampled_debug_reporting = true});
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
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner, loser;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_win_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(kTestBuyerDebugWinReportTemplate, winner.render()),
      &expected_buyer_win_report));
  // The buyer with the winning ad contributes its debug win report
  // to the response.
  EXPECT_THAT(debug_urls_map.at(winner.interest_group_owner()),
              EqualsProto(expected_buyer_win_report));

  ASSERT_TRUE(debug_urls_map.contains(loser.interest_group_owner()));
  DebugReports expected_buyer_loss_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(kTestBuyerDebugLossReportTemplate, loser.render()),
      &expected_buyer_loss_report));
  // The buyer with the losing ad contributes its debug loss report
  // to the response.
  EXPECT_THAT(debug_urls_map.at(loser.interest_group_owner()),
              EqualsProto(expected_buyer_loss_report));
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsEmptyDebugReportsToPutBuyersInSingleSellerAuctionToCooldown) {
  this->SetupRequest({.enable_sampled_debug_reporting = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsResponseMap expected_buyer_bids;
  // Both buyers have ads whose debug urls failed sampling.
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true,
                                    .debug_win_url_failed_sampling = true,
                                    .debug_loss_url_failed_sampling = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner, loser;
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_empty_win_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: true
          is_seller_report: false
          is_component_win: false
        }
      )pb",
      &expected_buyer_empty_win_report));
  // The buyer with the winning ad had its debug win url fail sampling. Thus, a
  // debug win report was not sent, and instead the buyer adtech needs to be put
  // in cooldown.
  EXPECT_THAT(debug_urls_map.at(winner.interest_group_owner()),
              EqualsProto(expected_buyer_empty_win_report));

  ASSERT_TRUE(debug_urls_map.contains(loser.interest_group_owner()));
  DebugReports expected_buyer_empty_loss_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: false
          is_seller_report: false
          is_component_win: false
        }
      )pb",
      &expected_buyer_empty_loss_report));
  // The buyer with the losing ad had its debug loss url fail sampling. Thus, a
  // debug loss report was not sent, and instead the buyer adtech needs to be
  // put in cooldown.
  EXPECT_THAT(debug_urls_map.at(loser.interest_group_owner()),
              EqualsProto(expected_buyer_empty_loss_report));
}

TYPED_TEST(
    SellerFrontEndServiceTest,
    SkipsSampledSellerReportIfSameAdtechHasBuyerReportInSingleSellerAuction) {
  this->SetupRequest({.num_buyers = 1,
                      .enable_sampled_debug_reporting = true,
                      .seller_adtech_is_also_a_buyer = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 1);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response);
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  std::string winner_render;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugWinReport, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner_render, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // The bid by the seller adtech (also a buyer in the auction) wins.
            AdScore score;
            const auto& bid = request->ad_bids(0);
            if (bid.interest_group_owner() == kSellerOriginDomain) {
              winner_render = bid.render();
              score.set_desirability(1);
              score.set_buyer_bid(1);
            }
            score.set_interest_group_name(bid.interest_group_name());
            score.set_interest_group_owner(bid.interest_group_owner());
            *response.mutable_ad_score() = score;
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Verify that there is only one debug report for the adtech that is both the
  // seller and buyer in the single-seller auction.
  auto& debug_urls_map = auction_result.adtech_origin_debug_urls_map();
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  DebugReports expected_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(kTestBuyerDebugWinReportTemplate, winner_render),
      &expected_reports));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain),
              EqualsProto(expected_reports));
}

TYPED_TEST(
    SellerFrontEndServiceTest,
    ReturnsMaxOneSampledDebugWinAndOneLossReportPerAdtechForComponentAuction) {
  this->SetupRequest({.num_buyers = 1,
                      .enable_sampled_debug_reporting = true,
                      .top_level_seller = kTestTopLevelSellerOriginDomain});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 1);
  BuyerBidsResponseMap expected_buyer_bids;
  // Single buyer has two ads: one wins and one loses in the component auction.
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        absl::StrCat(buyer_to_ad_url.at(buyer), "_0"),
        {.interest_group_name = "ig_0", .enable_debug_reporting = true});
    get_bid_response.mutable_bids()->Add(BuildNewAdWithBid(
        absl::StrCat(buyer_to_ad_url.at(buyer), "_1"),
        {.interest_group_name = "ig_1", .enable_debug_reporting = true}));
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner, loser;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_bid(1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              // Fields for component auction.
              score.set_ad_metadata(kTestAdMetadata);
              score.set_allow_component_auction(true);
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
            }
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_reports_if_winner_is_first;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat(
          absl::Substitute(kTestBuyerDebugWinReportForComponentWinnerTemplate,
                           winner.render()),
          absl::Substitute(kTestBuyerDebugLossReportForComponentWinnerTemplate,
                           winner.render()),
          R"pb(
            reports {
              url: ""
              is_win_report: false
              is_seller_report: false
              is_component_win: false
            }
          )pb"),
      &expected_buyer_reports_if_winner_is_first));
  DebugReports expected_buyer_reports_if_loser_is_first;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat(
          absl::Substitute(kTestBuyerDebugWinReportForComponentWinnerTemplate,
                           winner.render()),
          R"pb(
            reports {
              url: ""
              is_win_report: false
              is_seller_report: false
              is_component_win: true
            }
          )pb",
          absl::Substitute(kTestBuyerDebugLossReportTemplate, loser.render())),
      &expected_buyer_reports_if_loser_is_first));
  // The debug win url for the component auction winner is selected during
  // sampling and added to the response.
  // Depending on which ad is scored first, the debug loss url of either ad
  // could be selected, but not both, since there is a limit of one debug loss
  // url per adtech in component auctions.
  EXPECT_THAT(debug_urls_map.at(winner.interest_group_owner()).reports(),
              AnyOf(UnorderedPointwise(
                        EqualsProto(),
                        expected_buyer_reports_if_winner_is_first.reports()),
                    UnorderedPointwise(
                        EqualsProto(),
                        expected_buyer_reports_if_loser_is_first.reports())));

  // Verify seller debug reports.
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain),
              EqualsProto(seller_reports));
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsSampledDebugReportsForAllBuyersInComponentAuction) {
  this->SetupRequest({.enable_sampled_debug_reporting = true,
                      .top_level_seller = kTestTopLevelSellerOriginDomain});
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
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner, loser;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_bid(1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              // Fields for component auction.
              score.set_ad_metadata(kTestAdMetadata);
              score.set_allow_component_auction(true);
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
            }
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_winning_buyer_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(
          absl::StrCat(kTestBuyerDebugWinReportForComponentWinnerTemplate,
                       kTestBuyerDebugLossReportForComponentWinnerTemplate),
          winner.render()),
      &expected_winning_buyer_reports));
  // The buyer with the component auction winning ad contributes both its
  // debug win and loss urls to the response.
  EXPECT_THAT(debug_urls_map.at(winner.interest_group_owner()).reports(),
              UnorderedPointwise(EqualsProto(),
                                 expected_winning_buyer_reports.reports()));

  ASSERT_TRUE(debug_urls_map.contains(loser.interest_group_owner()));
  DebugReports expected_losing_buyer_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::Substitute(kTestBuyerDebugLossReportTemplate, loser.render()),
      &expected_losing_buyer_reports));
  // The buyer with the component auction losing ad contributes its debug loss
  // url to the response.
  EXPECT_THAT(debug_urls_map.at(loser.interest_group_owner()),
              EqualsProto(expected_losing_buyer_reports));
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsEachTypeOfEmptyDebugReportForBuyerInComponentAuction) {
  this->SetupRequest({.num_buyers = 1,
                      .enable_sampled_debug_reporting = true,
                      .top_level_seller = kTestTopLevelSellerOriginDomain});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 1);
  BuyerBidsResponseMap expected_buyer_bids;
  // Single buyer has two ads: one wins and one loses in the component auction.
  // Both ads had their debug urls fail sampling.
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        absl::StrCat(buyer_to_ad_url.at(buyer), "_0"),
        {.interest_group_name = "ig_0",
         .enable_debug_reporting = true,
         .debug_win_url_failed_sampling = true,
         .debug_loss_url_failed_sampling = true});
    get_bid_response.mutable_bids()->Add(
        BuildNewAdWithBid(absl::StrCat(buyer_to_ad_url.at(buyer), "_1"),
                          {.interest_group_name = "ig_1",
                           .enable_debug_reporting = true,
                           .debug_win_url_failed_sampling = true,
                           .debug_loss_url_failed_sampling = true}));
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_bid(1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              // Fields for component auction.
              score.set_ad_metadata(kTestAdMetadata);
              score.set_allow_component_auction(true);
              *response.mutable_ad_score() = score;
              if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
            }
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: true
          is_seller_report: false
          is_component_win: true
        }
        reports {
          url: ""
          is_win_report: false
          is_seller_report: false
          is_component_win: true
        }
        reports {
          url: ""
          is_win_report: false
          is_seller_report: false
          is_component_win: false
        }
      )pb",
      &expected_buyer_reports));
  // Debug reports with empty url should be added to the response for each of
  // the following urls that failed sampling: win url of component winner, loss
  // url of component winner, loss url of loser.
  EXPECT_THAT(
      debug_urls_map.at(winner.interest_group_owner()).reports(),
      UnorderedPointwise(EqualsProto(), expected_buyer_reports.reports()));

  // Verify seller debug reports.
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain),
              EqualsProto(seller_reports));
}

TYPED_TEST(SellerFrontEndServiceTest,
           ReturnsEmptyDebugReportsForAllBuyersInComponentAuction) {
  this->SetupRequest({.enable_sampled_debug_reporting = true,
                      .top_level_seller = kTestTopLevelSellerOriginDomain});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 2);
  BuyerBidsResponseMap expected_buyer_bids;
  // Both buyers have ads whose debug urls failed sampling.
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true,
                                    .debug_win_url_failed_sampling = true,
                                    .debug_loss_url_failed_sampling = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  AdScore winner, loser;
  DebugReports seller_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner, &loser, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            ScoreAdsResponse::ScoreAdsRawResponse response;
            // Last bid wins.
            for (int i = 0; i < request->ad_bids_size(); ++i) {
              const auto& bid = request->ad_bids(i);
              AdScore score;
              score.set_render(bid.render());
              score.mutable_component_renders()->CopyFrom(bid.ad_components());
              score.set_desirability(i + 1);
              score.set_buyer_bid(i + 1);
              score.set_bid(1);
              score.set_interest_group_name(bid.interest_group_name());
              score.set_interest_group_owner(bid.interest_group_owner());
              *response.mutable_ad_score() = score;
              // Fields for component auction.
              score.set_ad_metadata(kTestAdMetadata);
              score.set_allow_component_auction(true);
              *response.mutable_ad_score() = score;
              if (i < request->ad_bids().size() - 1) {
                loser = score;
              } else if (i == request->ad_bids().size() - 1) {
                winner = score;
              }
            }
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // Sampled debug reports should be present in the response.
  auto debug_urls_map = auction_result.adtech_origin_debug_urls_map();

  // Verify buyer debug reports.
  ASSERT_TRUE(debug_urls_map.contains(winner.interest_group_owner()));
  DebugReports expected_buyer_empty_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: true
          is_seller_report: false
          is_component_win: true
        }
        reports {
          url: ""
          is_win_report: false
          is_seller_report: false
          is_component_win: true
        }
      )pb",
      &expected_buyer_empty_reports));
  // The component winner had its debug win and loss urls fail sampling. Both
  // contribute a debug report with an empty url for the winning buyer.
  EXPECT_THAT(debug_urls_map.at(winner.interest_group_owner()).reports(),
              UnorderedPointwise(EqualsProto(),
                                 expected_buyer_empty_reports.reports()));

  ASSERT_TRUE(debug_urls_map.contains(loser.interest_group_owner()));
  DebugReports expected_buyer_empty_loss_report;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        reports {
          url: ""
          is_win_report: false
          is_seller_report: false
          is_component_win: false
        }
      )pb",
      &expected_buyer_empty_loss_report));
  // The component loser had its debug loss url fail sampling, so a debug
  // report with an empty url is added to the response for the losing buyer.
  EXPECT_THAT(debug_urls_map.at(loser.interest_group_owner()),
              EqualsProto(expected_buyer_empty_loss_report));
}

TYPED_TEST(
    SellerFrontEndServiceTest,
    MergesSampledBuyerAndSellerDebugReportsOfSameAdtechForComponentAuction) {
  this->SetupRequest({.num_buyers = 1,
                      .enable_sampled_debug_reporting = true,
                      .top_level_seller = kTestTopLevelSellerOriginDomain,
                      .seller_adtech_is_also_a_buyer = true});
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(this->request_);

  // Buyer Clients
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  int client_count = this->protected_auction_input_.buyer_input_size();
  EXPECT_EQ(client_count, 1);
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    // Buyer has one ad. Its debug win url passed sampling, and debug loss url
    // failed sampling.
    auto get_bid_response = BuildGetBidsResponseWithSingleAd(
        buyer_to_ad_url.at(buyer), {.enable_debug_reporting = true,
                                    .debug_loss_url_failed_sampling = true});
    SetupBuyerClientMock(buyer, buyer_clients, get_bid_response,
                         {.top_level_seller = kTestTopLevelSellerOriginDomain});
    expected_buyer_bids.try_emplace(
        buyer, std::make_unique<GetBidsResponse::GetBidsRawResponse>(
                   get_bid_response));
  }

  // Scoring signal provider
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  SetupScoringSignalsClient<TypeParam::kUseKvV2ForBrowser>(
      scoring_signals_provider, kv_async_client,
      std::move(expected_buyer_bids));

  // Scoring Client
  ScoringAsyncClientMock scoring_client;
  absl::Notification scoring_done;
  std::string winner_render;
  DebugReports seller_reports;
  // Both seller debug win and loss urls passed sampling.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      kTestSellerDebugReportsForComponentWinner, &seller_reports));
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .Times(1)
      .WillOnce(
          [&scoring_done, &winner_render, &seller_reports](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            // The bid by the seller adtech (also a buyer in the auction) wins.
            ScoreAdsResponse::ScoreAdsRawResponse response;
            AdScore score;
            const auto& bid = request->ad_bids(0);
            if (bid.interest_group_owner() == kSellerOriginDomain) {
              winner_render = bid.render();
              score.set_desirability(1);
              score.set_buyer_bid(1);
              score.set_bid(1);
            }
            score.set_render(bid.render());
            score.mutable_component_renders()->CopyFrom(bid.ad_components());
            score.set_interest_group_name(bid.interest_group_name());
            score.set_interest_group_owner(bid.interest_group_owner());
            // Fields for component auction.
            score.set_ad_metadata(kTestAdMetadata);
            score.set_allow_component_auction(true);
            *response.mutable_ad_score() = score;
            *response.mutable_seller_debug_reports() = seller_reports;
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
  // Debug pings should not be sent from the server if sampling is enabled.
  EXPECT_CALL(*async_reporter, DoReport).Times(0);

  MockEntriesCallOnBuyerFactory(this->protected_auction_input_.buyer_input(),
                                buyer_clients);

  // Client Registry
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);
  scoring_done.WaitForNotification();

  AuctionResult auction_result =
      DecryptBrowserAuctionResultAndNonce(
          *response.mutable_auction_result_ciphertext(), *this->context_)
          .first;

  // The buyer debug urls are processed first. Buyer's debug win url is added to
  // the response. Buyer's debug loss url failed sampling, so a corresponding
  // debug report with an empty url is added next.
  // Next, seller's debug win url is skipped since the adtech already has a
  // sampled debug win url. Finally, seller's debug loss url is added to the
  // response.
  auto& debug_urls_map = auction_result.adtech_origin_debug_urls_map();
  ASSERT_TRUE(debug_urls_map.contains(kSellerOriginDomain));
  DebugReports expected_reports;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat(
          absl::Substitute(kTestBuyerDebugWinReportForComponentWinnerTemplate,
                           winner_render),
          R"pb(
            reports {
              url: ""
              is_win_report: false
              is_seller_report: false
              is_component_win: true
            }
            reports {
              url: "https://seller.com/debugLoss"
              is_win_report: false
              is_seller_report: true
              is_component_win: true
            }
          )pb"),
      &expected_reports));
  EXPECT_THAT(debug_urls_map.at(kSellerOriginDomain).reports(),
              UnorderedPointwise(EqualsProto(), expected_reports.reports()));
}

auto EqGetBidsRawRequestKAnonFields(
    const GetBidsRequest::GetBidsRawRequest& raw_request) {
  return AllOf(Property(&GetBidsRequest::GetBidsRawRequest::enforce_kanon,
                        Eq(raw_request.enforce_kanon())),
               Property(&GetBidsRequest::GetBidsRawRequest::multi_bid_limit,
                        Eq(raw_request.multi_bid_limit())));
}

TYPED_TEST(SellerFrontEndServiceTest, VerifyKAnonFieldsPropagateToBuyers) {
  bool enable_kanon = true;
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bids_response =
        std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    auto* bid = get_bids_response->mutable_bids()->Add();
    bid->set_bid(10.0);
    bid->set_render("http://ad-render");
    expected_buyer_bids.try_emplace(buyer, std::move(get_bids_response));
  }
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  if (TypeParam::kUseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids);
  } else {
    SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                             {.scoring_signals_value = ""});
  }
  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  expected_get_bid_request.set_enforce_kanon(true);
  expected_get_bid_request.set_multi_bid_limit(10);

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.minimum_request_size, 0);
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(10.0);
        bid->set_render("http://ad-render");
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         &kv_async_client,
                         *key_fetcher_manager,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<typename TypeParam::InputType>(
          ClientType::CLIENT_TYPE_BROWSER, kSellerOriginDomain,
          /*is_consented_debug=*/false,
          /*top_level_seller=*/"",
          EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED,
          /*enable_unlimited_egress =*/false,
          /*enforce_kanon=*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);
  buyer_config.set_per_buyer_multi_bid_limit(10);

  RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request,
                                           this->executor_.get(), enable_kanon);
}

TYPED_TEST(SellerFrontEndServiceTest,
           VerifyKAnonFieldsDontPropagateToBuyersWhenFeatDisabled) {
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  for (const auto& [buyer, unused] :
       this->protected_auction_input_.buyer_input()) {
    auto get_bids_response =
        std::make_unique<GetBidsResponse::GetBidsRawResponse>();
    auto* bid = get_bids_response->mutable_bids()->Add();
    bid->set_bid(10.0);
    bid->set_render("http://ad-render");
    expected_buyer_bids.try_emplace(buyer, std::move(get_bids_response));
  }
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  if (TypeParam::kUseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids);
  } else {
    SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                             {.scoring_signals_value = ""});
  }
  // Scoring Client
  ScoringAsyncClientMock scoring_client;

  // Setup expectation on buyer client from SFE.
  GetBidsRequest::GetBidsRawRequest expected_get_bid_request;
  expected_get_bid_request.set_enforce_kanon(false);
  expected_get_bid_request.set_multi_bid_limit(0);

  auto mock_get_bids =
      [](std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
         grpc::ClientContext* context, GetBidDoneCallback on_done,
         absl::Duration timeout, RequestConfig request_config) {
        EXPECT_EQ(request_config.minimum_request_size, 0);
        auto get_bids_response =
            std::make_unique<GetBidsResponse::GetBidsRawResponse>();
        auto* bid = get_bids_response->mutable_bids()->Add();
        bid->set_bid(10.0);
        bid->set_render("http://ad-render");
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
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         &kv_async_client,
                         *key_fetcher_manager,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};
  auto [protected_auction_input, request, context] =
      GetSampleSelectAdRequest<typename TypeParam::InputType>(
          ClientType::CLIENT_TYPE_BROWSER, kSellerOriginDomain,
          /*is_consented_debug=*/false,
          /*top_level_seller=*/"",
          EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED,
          /*enable_unlimited_egress =*/false,
          /*enforce_kanon=*/true);

  request.mutable_auction_config()->set_seller_debug_id(kSampleSellerDebugId);
  auto& buyer_config = (*request.mutable_auction_config()
                             ->mutable_per_buyer_config())[kSampleBuyer];
  buyer_config.set_buyer_debug_id(kSampleBuyerDebugId);
  buyer_config.set_buyer_signals(kSampleBuyerSignals);
  buyer_config.set_per_buyer_multi_bid_limit(10);

  RunReactorRequest<SelectAdReactorForWeb>(this->config_, clients, request,
                                           this->executor_.get(),
                                           /*enable_kanon=*/false);
}

TYPED_TEST(SellerFrontEndServiceTest, ChaffingV1Enabled_SendsChaffRequest) {
  this->config_.SetOverride(kTrue, ENABLE_CHAFFING);

  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  // KV V2 Client
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  const float winning_bid = 999.0F;
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, winning_bid, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain, false);

  std::string chaff_buyer_name = "chaff_buyer";
  GetBidsResponse::GetBidsRawResponse response;

  // Set up buyer calls for chaff buyer.
  auto mock_get_bids =
      [response](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        // Verify this is a chaff request by validating the is_chaff and
        // generation_id fields.
        EXPECT_TRUE(get_bids_request->is_chaff());
        EXPECT_GT(request_config.minimum_request_size, 0);
        EXPECT_FALSE(get_bids_request->log_context().generation_id().empty());
        ABSL_LOG(INFO) << "Returning mock bids";
        std::move(on_done)(
            std::make_unique<GetBidsResponse::GetBidsRawResponse>(response),
            /*response_metadata=*/{});
        return absl::OkStatus();
      };
  auto setup_mock_buyer =
      [mock_get_bids](std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal)
            .Times(testing::Exactly(1))
            .WillRepeatedly(mock_get_bids);
        return buyer;
      };

  auto mock_buyer_Factory_call = [setup_mock_buyer](
                                     absl::string_view chaff_buyer_name) {
    return setup_mock_buyer(std::make_unique<BuyerFrontEndAsyncClientMock>());
  };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get(chaff_buyer_name))
      .Times(testing::Exactly(1))
      .WillRepeatedly(mock_buyer_Factory_call);

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

  auto mock_rng = std::make_unique<MockRandomNumberGenerator>();
  // For the ShouldSkipChaffing() call - do not skip chaffing in this case.
  EXPECT_CALL(*mock_rng, GetUniformReal(0, 1)).WillOnce(Return(1.0));
  // For generating the number of chaff requests to send.
  EXPECT_CALL(*mock_rng, GetUniformInt(1, 1)).WillOnce(Return(1));
  // For generating the chaff request size.
  EXPECT_CALL(*mock_rng, GetUniformInt(kMinChaffRequestSizeBytes,
                                       kMaxChaffRequestSizeBytes))
      .WillOnce(Return(kMaxChaffRequestSizeBytes));
  // Verify chaff request candidates are being shuffled and the first 'n' buyers
  // in the SFE config aren't chosen.
  EXPECT_CALL(*mock_rng, Shuffle)
      .WillOnce([](std::vector<absl::string_view>& vector) {
        std::reverse(vector.begin(), vector.end());
      });

  MockRandomNumberGeneratorFactory mock_rng_factory;
  EXPECT_CALL(mock_rng_factory, CreateRng)
      .WillOnce(Return(std::move(mock_rng)));

  // Append the chaff buyer to the auction_config field in the SelectAd
  // request.
  SelectAdRequest select_ad_request =
      std::move(request_with_context.select_ad_request);
  *select_ad_request.mutable_auction_config()->mutable_buyer_list()->Add() =
      chaff_buyer_name;
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, select_ad_request, this->executor_.get(),
          /* enable_kanon */ false,
          /* enable_buyer_private_aggregate_reporting */ false,
          /* per_adtech_paapi_contributions_limit */ 100,
          /* fail_fast */ false, /* report_win_map */ {}, mock_rng_factory);
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

TYPED_TEST(SellerFrontEndServiceTest,
           ChaffingV2Enabled_MovingMedianUpdatedOnlyForNonChaff) {
  this->config_.SetOverride(kTrue, ENABLE_CHAFFING);
  this->config_.SetOverride(kTrue, ENABLE_CHAFFING_V2);

  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  const float winning_bid = 999.0F;

  std::string chaff_buyer_name = "chaff_buyer";

  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, winning_bid, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain, true);

  // Set up buyer calls for the chaff buyer.
  absl::Notification chaff_buyer_called_notif;
  auto chaff_buyer_execute_mock_lambda =
      [&chaff_buyer_called_notif](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        EXPECT_TRUE(get_bids_request->is_chaff());
        std::move(on_done)(
            std::make_unique<GetBidsResponse::GetBidsRawResponse>(),
            /* response_metadata */ {});
        chaff_buyer_called_notif.Notify();
        return absl::OkStatus();
      };
  auto chaff_buyer_execute_mock =
      [chaff_buyer_execute_mock_lambda](
          std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal)
            .WillRepeatedly(chaff_buyer_execute_mock_lambda);
        return buyer;
      };
  auto chaff_buyer_client_mock =
      [chaff_buyer_execute_mock](absl::string_view chaff_buyer_name) {
        return chaff_buyer_execute_mock(
            std::make_unique<BuyerFrontEndAsyncClientMock>());
      };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get(chaff_buyer_name))
      .WillRepeatedly(chaff_buyer_client_mock);

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

  auto mock_rng = std::make_unique<MockRandomNumberGenerator>();
  // For the ShouldSkipChaffing() call - do not skip chaffing in this case.
  EXPECT_CALL(*mock_rng, GetUniformReal(0, 1)).WillOnce(Return(1.0));
  // For generating the number of chaff requests to send.
  EXPECT_CALL(*mock_rng, GetUniformInt(1, 1)).WillOnce(Return(1));

  MockRandomNumberGeneratorFactory mock_rng_factory;
  EXPECT_CALL(mock_rng_factory, CreateRng)
      .WillOnce(Return(std::move(mock_rng)));

  auto mock_mmm = std::make_unique<MockMovingMedianManager>();
  EXPECT_CALL(*mock_mmm, IsWindowFilled)
      .Times(testing::Exactly(2))
      .WillOnce([&](absl::string_view buyer) {
        EXPECT_EQ(buyer, kSampleBuyer);
        return true;  // Can be false too; doesn't matter for this test.
      })
      .WillOnce([&](absl::string_view buyer) {
        EXPECT_EQ(buyer, chaff_buyer_name);
        return true;  // Can be false too; doesn't matter for this test.
      });
  EXPECT_CALL(*mock_mmm, AddNumberToBuyerWindow)
      .Times(testing::Exactly(1))
      .WillOnce(
          [&](absl::string_view buyer, RandomNumberGenerator& rng, int val) {
            EXPECT_EQ(buyer, kSampleBuyer);
            return absl::OkStatus();
          });
  clients.moving_median_manager = std::move(mock_mmm);

  SelectAdRequest select_ad_request =
      std::move(request_with_context.select_ad_request);
  // Append the chaff buyer to the auction_config field in the SelectAd
  // request.
  *select_ad_request.mutable_auction_config()->mutable_buyer_list()->Add() =
      chaff_buyer_name;
  SelectAdResponse encrypted_response =
      RunReactorRequest<SelectAdReactorForWeb>(
          this->config_, clients, select_ad_request, this->executor_.get(),
          /* enable_kanon */ false,
          /* enable_buyer_private_aggregate_reporting */ false,
          /* per_adtech_paapi_contributions_limit */ 100,
          /* fail_fast */ false, /* report_win_map */ {}, mock_rng_factory);
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

TYPED_TEST(SellerFrontEndServiceTest, VerifySameBuyersInvokedOnRequestReplay) {
  this->config_.SetOverride(kTrue, ENABLE_CHAFFING);
  this->config_.SetOverride(kTrue, ENABLE_BUYER_CACHING);

  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  ScoringAsyncClientMock scoring_client;
  KVAsyncClientMock kv_async_client;
  BuyerFrontEndAsyncClientFactoryMock buyer_front_end_async_client_factory_mock;
  BuyerBidsResponseMap expected_buyer_bids;
  std::unique_ptr<server_common::MockKeyFetcherManager> key_fetcher_manager =
      std::make_unique<server_common::MockKeyFetcherManager>();
  EXPECT_CALL(*key_fetcher_manager, GetPrivateKey)
      .WillRepeatedly(Return(GetPrivateKey()));
  const float winning_bid = 999.0F;

  const std::string non_chaff_buyer_name = std::string(kSampleBuyer.data());
  std::string chaff_buyer_name = "chaff_buyer";

  std::string generation_id = MakeARandomString();
  auto [request_with_context, clients] =
      GetSelectAdRequestAndClientRegistryForTest<typename TypeParam::InputType,
                                                 TypeParam::kUseKvV2ForBrowser>(
          CLIENT_TYPE_BROWSER, winning_bid, &scoring_signals_provider,
          scoring_client, buyer_front_end_async_client_factory_mock,
          &kv_async_client, key_fetcher_manager.get(), expected_buyer_bids,
          kSellerOriginDomain, false, "", false, false, {}, false, {}, nullptr,
          generation_id);
  std::unique_ptr<MockCache<std::string, InvokedBuyers>> invoked_buyers_cache =
      std::make_unique<MockCache<std::string, InvokedBuyers>>();
  EXPECT_CALL(*invoked_buyers_cache, Query)
      .Times(testing::Exactly(1))
      .WillRepeatedly([&](const absl::flat_hash_set<std::string>&) {
        InvokedBuyers invoked_buyers = {
            .chaff_buyer_names = {chaff_buyer_name},
            .non_chaff_buyer_names = {non_chaff_buyer_name}};
        absl::flat_hash_map<std::string, InvokedBuyers> map;
        map.insert({generation_id, invoked_buyers});
        return map;
      });
  clients.invoked_buyers_cache = std::move(invoked_buyers_cache);

  this->SetupRequest({.num_buyers = 1});
  absl::Notification non_chaff_buyer_called_notif;
  auto non_chaff_buyer_execute_mock_lambda =
      [&non_chaff_buyer_called_notif](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        EXPECT_FALSE(get_bids_request->is_chaff());
        std::move(on_done)(
            std::make_unique<GetBidsResponse::GetBidsRawResponse>(),
            /* response_metadata= */ {});
        non_chaff_buyer_called_notif.Notify();
        return absl::OkStatus();
      };
  auto non_chaff_buyer_execute_mock =
      [non_chaff_buyer_execute_mock_lambda](
          std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal)
            .WillRepeatedly(non_chaff_buyer_execute_mock_lambda);
        return buyer;
      };
  auto non_chaff_buyer_client_mock =
      [non_chaff_buyer_execute_mock](absl::string_view chaff_buyer_name) {
        return non_chaff_buyer_execute_mock(
            std::make_unique<BuyerFrontEndAsyncClientMock>());
      };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock,
              Get(non_chaff_buyer_name))
      .WillRepeatedly(non_chaff_buyer_client_mock);

  // Set up buyer calls for chaff buyer 1.
  absl::Notification chaff_buyer_called_notif;
  auto chaff_buyer_execute_mock_lambda =
      [&chaff_buyer_called_notif](
          std::unique_ptr<GetBidsRequest::GetBidsRawRequest> get_bids_request,
          grpc::ClientContext* context, GetBidDoneCallback on_done,
          absl::Duration timeout, RequestConfig request_config) {
        EXPECT_TRUE(get_bids_request->is_chaff());
        std::move(on_done)(
            std::make_unique<GetBidsResponse::GetBidsRawResponse>(),
            /*response_metadata=*/{});
        chaff_buyer_called_notif.Notify();
        return absl::OkStatus();
      };
  auto chaff_buyer_execute_mock =
      [chaff_buyer_execute_mock_lambda](
          std::unique_ptr<BuyerFrontEndAsyncClientMock> buyer) {
        EXPECT_CALL(*buyer, ExecuteInternal)
            .WillRepeatedly(chaff_buyer_execute_mock_lambda);
        return buyer;
      };
  auto chaff_buyer_client_mock =
      [chaff_buyer_execute_mock](absl::string_view chaff_buyer_name) {
        return chaff_buyer_execute_mock(
            std::make_unique<BuyerFrontEndAsyncClientMock>());
      };
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Get(chaff_buyer_name))
      .WillRepeatedly(chaff_buyer_client_mock);

  // Mock Entries() on the buyer factory return the 'real' and chaff buyers.
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries;
  entries.emplace_back(kSampleBuyer,  // The non-chaff buyer.
                       std::make_shared<BuyerFrontEndAsyncClientMock>());
  entries.emplace_back(chaff_buyer_name,
                       std::make_shared<BuyerFrontEndAsyncClientMock>());
  EXPECT_CALL(buyer_front_end_async_client_factory_mock, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  auto mock_rng = std::make_unique<MockRandomNumberGenerator>();
  // For generating the chaff request size.
  EXPECT_CALL(*mock_rng, GetUniformInt(kMinChaffRequestSizeBytes,
                                       kMaxChaffRequestSizeBytes))
      .WillOnce(Return(kMaxChaffRequestSizeBytes));

  MockRandomNumberGeneratorFactory mock_rng_factory;
  EXPECT_CALL(mock_rng_factory, CreateRng)
      .WillOnce(Return(std::move(mock_rng)));

  SelectAdRequest select_ad_request =
      std::move(request_with_context.select_ad_request);

  (void)RunReactorRequest<SelectAdReactorForWeb>(
      this->config_, clients, select_ad_request, this->executor_.get(),
      /* enable_kanon */ false,
      /* enable_buyer_private_aggregate_reporting */ true,
      /* per_adtech_paapi_contributions_limit */ 100,
      /* fail_fast */ false, /* report_win_map */ {}, mock_rng_factory);

  non_chaff_buyer_called_notif.WaitForNotification();
  chaff_buyer_called_notif.WaitForNotification();
}

TYPED_TEST(SellerFrontEndServiceTest, VerifyPrioritySignals) {
  const absl::string_view kPriorityJsonTemplate =
      R"({"priority_field_key":$0})";

  this->config_.SetOverride(kTrue, ENABLE_PRIORITY_VECTOR);
  this->SetupRequest({.set_buyer_egid = true});
  this->request_.mutable_auction_config()->set_priority_signals("{}");

  // Collect the priority signals JSONs that each buyer is expected to receive
  // in a set. Below, we'll verify each of the expected JSONs is sent out in a
  // BFE request. The JSONs are collected in a set because we don't have a way
  // to map from <BuyerName, PrioritySignalsJson> below where we do the check
  // for these JSONs. So we just check that each JSON in the set was sent in one
  // of the BFE request objects.
  absl::flat_hash_set<std::string> expected_priority_signals_jsons;
  for (int i = 0; i < this->request_.auction_config().buyer_list().size();
       i++) {
    const auto& buyer_ig_owner =
        this->request_.auction_config().buyer_list()[i];
    auto& buyer_config = (*this->request_.mutable_auction_config()
                               ->mutable_per_buyer_config())[buyer_ig_owner];

    double overridden_value = i;
    std::string priority_signals_overrides_json =
        absl::Substitute(kPriorityJsonTemplate, FormatNumber(overridden_value));
    buyer_config.set_priority_signals_overrides(
        priority_signals_overrides_json);

    expected_priority_signals_jsons.insert(priority_signals_overrides_json);
  }

  ScoringAsyncClientMock scoring_client;
  MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>
      scoring_signals_provider;
  BuyerFrontEndAsyncClientFactoryMock buyer_clients;
  KVAsyncClientMock kv_async_client;

  bool success = true;
  auto setup_mock_buyer = [&expected_priority_signals_jsons, &success](
                              const BuyerInputForBidding& buyer_input,
                              absl::string_view buyer_ig_owner) {
    auto buyer = std::make_unique<BuyerFrontEndAsyncClientMock>();
    EXPECT_CALL(*buyer, ExecuteInternal)
        .Times(1)
        .WillOnce([&expected_priority_signals_jsons, &success](
                      std::unique_ptr<GetBidsRequest::GetBidsRawRequest>
                          get_bids_request,
                      grpc::ClientContext* context, GetBidDoneCallback on_done,
                      absl::Duration timeout, RequestConfig request_config) {
          const std::string& per_buyer_priority_signals =
              get_bids_request->priority_signals();
          // Check to see if the priority signals JSON for this BFE request is
          // in the set of expected JSONs.
          if (!expected_priority_signals_jsons.contains(
                  per_buyer_priority_signals)) {
            success = false;
          }
          expected_priority_signals_jsons.erase(per_buyer_priority_signals);

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
    const BuyerInputForBidding& buyer_input = DecodeBuyerInput(
        buyer_ig_owner,
        this->protected_auction_input_.buyer_input().at(buyer_ig_owner),
        error_accumulator);
    EXPECT_CALL(buyer_clients, Get(buyer_ig_owner))
        .WillOnce([setup_mock_buyer, buyer_input](absl::string_view hostname) {
          return setup_mock_buyer(buyer_input, hostname);
        });

    entries.emplace_back(buyer_ig_owner,
                         std::make_shared<BuyerFrontEndAsyncClientMock>());
  }

  EXPECT_CALL(buyer_clients, Entries)
      .WillRepeatedly(Return(std::move(entries)));

  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());
  ClientRegistry clients{&scoring_signals_provider,
                         scoring_client,
                         buyer_clients,
                         &kv_async_client,
                         this->key_fetcher_manager_,
                         /*crypto_client=*/nullptr,
                         std::move(async_reporter)};

  Response response = RunRequest<SelectAdReactorForWeb>(
      this->config_, clients, this->request_, this->executor_.get(),
      this->report_win_map_);

  EXPECT_TRUE(success) << "The expected priority signal JSONs were not all "
                          "present in the BFE requests";
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
