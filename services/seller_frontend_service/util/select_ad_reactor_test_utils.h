/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "services/common/compression/gzip.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/test/utils/ohttp_utils.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_mock.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/test/app_test_utils.h"
#include "services/seller_frontend_service/test/constants.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kAuctionHost[] = "auction-server.com";
constexpr char kSellerOriginDomain[] = "seller.com";
constexpr char kTestTopLevelSellerOriginDomain[] = "top-level-seller.com";
constexpr double kAdCost = 1.0;
constexpr int kModelingSignals = 0;
constexpr int kDefaultNumAdComponents = 3;
constexpr absl::string_view kSampleInterestGroupName = "interest_group";
constexpr absl::string_view kEmptyBuyer = "";
constexpr absl::string_view kSampleBuyer = "https://ad_tech_A.com";
constexpr absl::string_view kSampleBuyer2 = "https://ad_tech_B.com";
constexpr absl::string_view kSampleBuyer3 = "https://ad_tech_C.com";
constexpr absl::string_view kSampleSellerDebugId = "sample-seller-debug-id";
constexpr absl::string_view kSampleBuyerDebugId = "sample-buyer-debug-id";
constexpr absl::string_view kSampleBuyerSignals = "[]";
constexpr absl::string_view kSampleContextualPasAdId = "test-ad-id";
constexpr float kNonZeroBidValue = 1.0;
constexpr float kZeroBidValue = 0.0;
constexpr int kNonZeroDesirability = 1;
constexpr bool kIsConsentedDebug = true;
inline constexpr char kTestEvent[] = "click";
inline constexpr char kTestInteractionUrl[] = "http://click.com";
inline constexpr char kTestTopLevelSellerReportingUrl[] =
    "http://reportResult.com";
inline constexpr char kTestComponentSellerReportingUrl[] =
    "http://componentReportResult.com";
inline constexpr char kTestBuyerReportingUrl[] = "http://reportWin.com";
inline constexpr char kTestAdMetadata[] = "testAdMetadata";
inline constexpr char kEurosIsoCode[] = "EUR";
inline constexpr char kUsdIsoCode[] = "USD";
inline constexpr char kYenIsoCode[] = "JPY";
inline constexpr uint32_t kDefaultDataVersion = 1689;
inline constexpr uint32_t kDefaultSellerDataVersion = 1776;
inline constexpr char kValidScoringSignalsJson[] =
    R"JSON({"someAdRenderUrl":{"someKey":"someValue"}})JSON";
inline constexpr char kValidScoringSignalsJsonKvV2[] =
    R"JSON({"renderUrls":{"someKey":"someValue"}})JSON";
inline constexpr char kKvV2CompressionGroup[] =
    R"pb(compression_groups {
           compression_group_id: 33
           content: "[{\"id\":0,\"keyGroupOutputs\":[{\"tags\":[\"renderUrls\"],\"keyValues\":{\"someKey\":{\"value\":\"someValue\"}}}]}]"
         })pb";
constexpr absl::string_view kSampleBiddingUrl = "https://ad_tech_A.com/bid.js";
constexpr absl::string_view kSampleBiddingUrl2 = "https://ad_tech_B.com/bid.js";
constexpr absl::string_view kSampleBiddingUrl3 = "https://ad_tech_C.com/bid.js";
constexpr absl::string_view kTestRenderUrlSuffix = "/ad";
constexpr absl::string_view kTestComponentUrlSuffix = "/ad-component-";
constexpr absl::string_view kTestBuyerDebugWinUrlPrefix =
    "https://buyer.com/debugWin?render=";
constexpr absl::string_view kTestBuyerDebugLossUrlPrefix =
    "https://buyer.com/debugLoss?render=";
constexpr absl::string_view kTestSellerDebugWinUrlPrefix =
    "https://seller.com/debugWin?render=";
constexpr absl::string_view kTestSellerDebugLossUrlPrefix =
    "https://seller.com/debugLoss?render=";

template <typename T>
struct EncryptedSelectAdRequestWithContext {
  // Clear text protected auction input.
  T protected_auction_input;
  // Request containing the ciphertext blob of protected audience input.
  SelectAdRequest select_ad_request;
  // OHTTP request context used to encrypt the plain text protected audience
  // input. (Useful for decoding the response).
  quiche::ObliviousHttpRequest::Context context;
};

absl::flat_hash_map<std::string, std::string> BuildBuyerWinningAdUrlMap(
    const SelectAdRequest& request);

struct BuildNewAdWithBidOptions {
  absl::string_view interest_group_name = "";
  const float bid_value = 1.0f;
  const bool enable_event_level_debug_reporting = false;
  const int number_ad_component_render_urls = kDefaultNumAdComponents;
  absl::string_view bid_currency = "";
  absl::string_view buyer_reporting_id = "";
  absl::string_view buyer_and_seller_reporting_id = "";
  absl::string_view selected_buyer_and_seller_reporting_id = "";
  uint32_t data_version = kDefaultDataVersion;
  std::vector<PrivateAggregateContribution> contributions;
};

GetBidsResponse::GetBidsRawResponse BuildGetBidsResponseWithSingleAd(
    absl::string_view ad_url,
    const BuildNewAdWithBidOptions& options = BuildNewAdWithBidOptions{});

void SetupMockCryptoClient(MockCryptoClientWrapper& crypto_client);

struct SetupBuyerClientMockOptions {
  bool repeated_get_allowed = false;
  bool expect_all_buyers_solicited = true;
  int* num_buyers_solicited;
  absl::string_view top_level_seller = "";
};

void SetupBuyerClientMock(
    absl::string_view hostname,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients,
    const std::optional<GetBidsResponse::GetBidsRawResponse>& bid,
    const SetupBuyerClientMockOptions& options = {});

AdWithBid BuildAdWithBidFromAdWithBidMetadata(
    const ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata& input,
    absl::string_view buyer_reporting_id = "",
    absl::string_view buyer_and_seller_reporting_id = "",
    absl::string_view selected_buyer_and_seller_reporting_id = "");

AdWithBid BuildNewAdWithBid(
    absl::string_view ad_url,
    const BuildNewAdWithBidOptions& options = BuildNewAdWithBidOptions{});

ProtectedAppSignalsAdWithBid BuildNewPASAdWithBid(
    const std::string& ad_render_url, absl::optional<float> bid_value,
    const bool enable_event_level_debug_reporting,
    absl::optional<absl::string_view> bid_currency);

struct ScoringProviderMockOptions {
  const std::string scoring_signals_value = kValidScoringSignalsJson;
  const uint32_t data_version = kDefaultSellerDataVersion;
  bool repeated_get_allowed = false;
  const absl::Status server_status_to_return =
      absl::Status(absl::StatusCode::kOk, "OK");
  int expected_num_bids = -1;
  const std::string seller_egid;
};

// TODO(b/374118522): Merge with ScoringProviderMockOptions.
struct KvAsyncMockOptions {
  bool repeated_get_allowed = false;
  const absl::Status server_status_to_return =
      absl::Status(absl::StatusCode::kOk, "OK");
  int expected_num_ads = -1;
  const std::string seller_egid;
};

/**
 * Cannot implement go/totw/176 and return provider, as MockAsyncProvider has an
 * explicitly-deleted copy constructor. Named Return Value Optimization
 * (discussed in go/totw/11) would ensure that a copy is not performed, and
 * C++17 guarantees that NVRO will be performed when possible, but it still
 * requires a copy constructor to be present. We could return a unique_ptr, but
 * the ClientRegistry API to which this is fed expects a const reference, and at
 * that point with the unintuitive dereference makes the current implementation
 * simpler.
 */
void SetupScoringProviderMock(
    const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>& provider,
    const BuyerBidsResponseMap& expected_buyer_bids,
    const ScoringProviderMockOptions& options = ScoringProviderMockOptions{});

void SetupKvAsyncClientMock(
    KVAsyncClientMock& kv_async_client,
    const kv_server::v2::GetValuesResponse& response,
    const BuyerBidsResponseMap& expected_buyer_bids,
    const KvAsyncMockOptions& options = KvAsyncMockOptions{});

std::vector<AdWithBid> GetAdWithBidsInMultipleCurrencies(
    int num_ad_with_bids, int num_mismatched,
    absl::string_view matching_currency, absl::string_view mismatching_currency,
    absl::string_view base_ad_render_url, absl::string_view base_ig_name);

std::vector<ProtectedAppSignalsAdWithBid> GetPASAdWithBidsInMultipleCurrencies(
    const int num_ad_with_bids, const int num_mismatched,
    absl::string_view matching_currency, absl::string_view mismatching_currency,
    absl::string_view base_ad_render_url);

void MockEntriesCallOnBuyerFactory(
    const google::protobuf::Map<std::string, std::string>& buyer_input,
    const BuyerFrontEndAsyncClientFactoryMock& factory);

TrustedServersConfigClient CreateConfig();

template <class T>
SelectAdResponse RunRequest(
    const TrustedServersConfigClient& config_client,
    const ClientRegistry& clients, const SelectAdRequest& request,
    server_common::Executor* executor, const ReportWinMap& report_win_map,
    const int max_buyers_solicited = 2, const bool enable_kanon = false,
    const bool enable_buyer_private_aggregate_reporting = false,
    int per_adtech_paapi_contributions_limit = 100) {
  grpc::CallbackServerContext context;
  SelectAdResponse response;
  T reactor(&context, &request, &response, executor, clients, config_client,
            report_win_map,
            /*enable_cancellation=*/false, enable_kanon,
            enable_buyer_private_aggregate_reporting,
            per_adtech_paapi_contributions_limit,
            /*fail_fast=*/true, max_buyers_solicited);
  reactor.Execute();
  return response;
}

server_common::PrivateKey GetPrivateKey();

template <typename T>
BuyerBidsResponseMap GetBuyerClientsAndBidsForReactor(
    const SelectAdRequest& request, const T& protected_auction_input,
    const BuyerFrontEndAsyncClientFactoryMock& buyer_clients,
    bool expect_all_buyers_solicited = true) {
  BuyerBidsResponseMap buyer_bids;
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request);

  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    const std::string& ad_url = buyer_to_ad_url.at(local_buyer);
    AdWithBid bid = BuildNewAdWithBid(
        ad_url, {.interest_group_name = kSampleInterestGroupName});
    GetBidsResponse::GetBidsRawResponse response;
    auto mutable_bids = response.mutable_bids();
    mutable_bids->Add(std::move(bid));

    SetupBuyerClientMock(
        local_buyer, buyer_clients, response,
        {.repeated_get_allowed = true,
         .expect_all_buyers_solicited = expect_all_buyers_solicited});
    buyer_bids.try_emplace(
        local_buyer,
        std::make_unique<GetBidsResponse::GetBidsRawResponse>(response));
  }

  return buyer_bids;
}

std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetFramedInputAndOhttpContext(absl::string_view encoded_request);

template <typename T>
std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetCborEncodedEncryptedInputAndOhttpContext(const T& protected_auction_input) {
  absl::StatusOr<std::string> encoded_request =
      CborEncodeProtectedAuctionProto(protected_auction_input);
  EXPECT_TRUE(encoded_request.ok()) << encoded_request.status();
  return GetFramedInputAndOhttpContext(*encoded_request);
}

// Gets the encoded and encrypted request as well as the OHTTP context used
// for encrypting the request.
template <typename T>
std::pair<std::string, quiche::ObliviousHttpRequest::Context>
GetProtoEncodedEncryptedInputAndOhttpContext(const T& protected_auction_input) {
  return GetFramedInputAndOhttpContext(
      protected_auction_input.SerializeAsString());
}

template <typename T>
EncryptedSelectAdRequestWithContext<T> GetSampleSelectAdRequest(
    ClientType client_type, absl::string_view seller_origin_domain,
    bool is_consented_debug = false, absl::string_view top_level_seller = "",
    EncryptionCloudPlatform top_seller_cloud_platform =
        EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED,
    bool enable_unlimited_egress = false, bool enforce_kanon = false,
    std::string generation_id = kSampleGenerationId) {
  BuyerInputForBidding buyer_input;
  auto* interest_group = buyer_input.mutable_interest_groups()->Add();
  interest_group->set_name(kSampleInterestGroupName);
  *interest_group->mutable_bidding_signals_keys()->Add() = "[]";
  google::protobuf::RepeatedPtrField<std::string> ad_render_ids;
  ad_render_ids.Add(MakeARandomString());
  interest_group->mutable_browser_signals()->CopyFrom(
      MakeRandomBrowserSignalsForBiddingForIG(ad_render_ids));
  google::protobuf::Map<std::string, BuyerInputForBidding> decoded_buyer_inputs;
  decoded_buyer_inputs.emplace(kSampleBuyer, buyer_input);
  google::protobuf::Map<std::string, std::string> encoded_buyer_inputs;
  switch (client_type) {
    case CLIENT_TYPE_BROWSER:
      encoded_buyer_inputs = *GetEncodedBuyerInputMap(decoded_buyer_inputs);
      break;
    case CLIENT_TYPE_ANDROID:
      encoded_buyer_inputs = GetProtoEncodedBuyerInputs(decoded_buyer_inputs);
      break;
    default:
      EXPECT_TRUE(false)
          << "Test configuration error, unsupported client type: "
          << client_type;
      break;
  }

  SelectAdRequest request;
  T protected_auction_input;
  protected_auction_input.set_generation_id(generation_id);
  protected_auction_input.set_enable_unlimited_egress(enable_unlimited_egress);
  protected_auction_input.set_enforce_kanon(enforce_kanon);
  if (is_consented_debug) {
    auto* consented_debug_config =
        protected_auction_input.mutable_consented_debug_config();
    consented_debug_config->set_is_consented(kIsConsentedDebug);
    consented_debug_config->set_token(kConsentedDebugToken);
  }

  *protected_auction_input.mutable_buyer_input() =
      std::move(encoded_buyer_inputs);
  request.mutable_auction_config()->set_seller_signals(
      absl::StrCat("{\"seller_signal\": \"", MakeARandomString(), "\"}"));
  request.mutable_auction_config()->set_auction_signals(
      absl::StrCat("{\"auction_signal\": \"", MakeARandomString(), "\"}"));
  request.mutable_auction_config()
      ->mutable_code_experiment_spec()
      ->set_score_ad_version("bucket/test");
  for (const auto& [local_buyer, unused] :
       protected_auction_input.buyer_input()) {
    *request.mutable_auction_config()->mutable_buyer_list()->Add() =
        local_buyer;
  }
  protected_auction_input.set_publisher_name(MakeARandomString());
  request.mutable_auction_config()->set_seller(seller_origin_domain);
  request.mutable_auction_config()->set_top_level_cloud_platform(
      top_seller_cloud_platform);
  if (!top_level_seller.empty()) {
    request.mutable_auction_config()->set_top_level_seller(top_level_seller);
  }
  request.set_client_type(client_type);

  const auto* descriptor = protected_auction_input.GetDescriptor();
  const bool is_protected_auction_input =
      descriptor->name() == kProtectedAuctionInput;

  switch (client_type) {
    case CLIENT_TYPE_ANDROID: {
      auto [encrypted_request, context] =
          GetProtoEncodedEncryptedInputAndOhttpContext(protected_auction_input);
      if (is_protected_auction_input) {
        *request.mutable_protected_auction_ciphertext() =
            std::move(encrypted_request);
      } else {
        *request.mutable_protected_audience_ciphertext() =
            std::move(encrypted_request);
      }
      return {std::move(protected_auction_input), std::move(request),
              std::move(context)};
    }
    case CLIENT_TYPE_BROWSER:
    default: {
      auto [encrypted_request, context] =
          GetCborEncodedEncryptedInputAndOhttpContext(protected_auction_input);
      if (is_protected_auction_input) {
        *request.mutable_protected_auction_ciphertext() =
            std::move(encrypted_request);
      } else {
        *request.mutable_protected_audience_ciphertext() =
            std::move(encrypted_request);
      }
      return {std::move(protected_auction_input), std::move(request),
              std::move(context)};
    }
  }
}

struct ServerComponentAuctionParams {
  MockCryptoClientWrapper* crypto_client = nullptr;
  EncryptionCloudPlatform top_level_cloud_platform =
      EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_UNSPECIFIED;
};

template <typename T, bool UseKvV2>
std::pair<EncryptedSelectAdRequestWithContext<T>, ClientRegistry>
GetSelectAdRequestAndClientRegistryForTest(
    ClientType client_type, std::optional<float> buyer_bid,
    absl::Nullable<
        const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>* const>
        scoring_signals_provider,
    ScoringAsyncClientMock& scoring_client,
    const BuyerFrontEndAsyncClientFactoryMock&
        buyer_front_end_async_client_factory_mock,
    absl::Nullable<KVAsyncClientMock* const> kv_async_client,
    server_common::MockKeyFetcherManager* mock_key_fetcher_manager,
    BuyerBidsResponseMap& expected_buyer_bids,
    absl::string_view seller_origin_domain,
    bool expect_all_buyers_solicited = true,
    absl::string_view top_level_seller = "", bool enable_reporting = false,
    bool force_set_modified_bid_to_zero = false,
    ServerComponentAuctionParams server_component_auction_params = {},
    bool enforce_kanon = false,
    const std::vector<ScoreAdsResponse::AdScore>& kanon_ghost_winners = {},
    std::unique_ptr<KAnonCacheManagerMock> k_anon_cache_manager = nullptr,
    std::string generation_id = kSampleGenerationId) {
  auto encrypted_request_with_context = GetSampleSelectAdRequest<T>(
      client_type, seller_origin_domain,
      /*is_consented_debug=*/false, top_level_seller,
      server_component_auction_params.top_level_cloud_platform,
      /*enable_unlimited_egress=*/false, enforce_kanon, generation_id);

  // Sets up buyer client while populating the expected buyer bids that can
  // then be used to setup the scoring signals provider.
  expected_buyer_bids = GetBuyerClientsAndBidsForReactor(
      encrypted_request_with_context.select_ad_request,
      encrypted_request_with_context.protected_auction_input,
      buyer_front_end_async_client_factory_mock, expect_all_buyers_solicited);

  // Scoring signals provider
  if (UseKvV2) {
    kv_server::v2::GetValuesResponse kv_response;
    EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
        kKvV2CompressionGroup, &kv_response));
    if (kv_async_client != nullptr) {
      SetupKvAsyncClientMock(*kv_async_client, kv_response, expected_buyer_bids,
                             {.repeated_get_allowed = true});
    }
  } else {
    if (scoring_signals_provider != nullptr) {
      SetupScoringProviderMock(*scoring_signals_provider, expected_buyer_bids,
                               {.repeated_get_allowed = true});
    }
  }
  float bid_value = kNonZeroBidValue;
  if (buyer_bid) {
    bid_value = *buyer_bid;
  }
  using ScoreAdsDoneCallback =
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                  ScoreAdsResponse::ScoreAdsRawResponse>>,
                              ResponseMetadata) &&>;
  // Sets up scoring Client
  EXPECT_CALL(scoring_client, ExecuteInternal)
      .WillRepeatedly(
          [bid_value, client_type, top_level_seller, enable_reporting,
           force_set_modified_bid_to_zero, kanon_ghost_winners](
              std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
              grpc::ClientContext* context, ScoreAdsDoneCallback on_done,
              absl::Duration timeout, RequestConfig request_config) {
            for (const auto& bid : request->ad_bids()) {
              auto response =
                  std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>();
              ScoreAdsResponse::AdScore* score = response->mutable_ad_score();
              EXPECT_FALSE(bid.render().empty());
              score->set_render(bid.render());
              score->mutable_component_renders()->CopyFrom(bid.ad_components());
              EXPECT_EQ(bid.ad_components_size(), kDefaultNumAdComponents);
              score->set_desirability(kNonZeroDesirability);
              score->set_buyer_bid(bid_value);
              score->set_interest_group_name(bid.interest_group_name());
              score->set_interest_group_owner(kSampleBuyer);
              if (enable_reporting) {
                score->mutable_win_reporting_urls()
                    ->mutable_top_level_seller_reporting_urls()
                    ->set_reporting_url(kTestTopLevelSellerReportingUrl);
                score->mutable_win_reporting_urls()
                    ->mutable_top_level_seller_reporting_urls()
                    ->mutable_interaction_reporting_urls()
                    ->try_emplace(kTestEvent, kTestInteractionUrl);
                score->mutable_win_reporting_urls()
                    ->mutable_buyer_reporting_urls()
                    ->set_reporting_url(kTestBuyerReportingUrl);
                score->mutable_win_reporting_urls()
                    ->mutable_buyer_reporting_urls()
                    ->mutable_interaction_reporting_urls()
                    ->try_emplace(kTestEvent, kTestInteractionUrl);
                score->mutable_win_reporting_urls()
                    ->mutable_component_seller_reporting_urls()
                    ->set_reporting_url(kTestComponentSellerReportingUrl);
                score->mutable_win_reporting_urls()
                    ->mutable_component_seller_reporting_urls()
                    ->mutable_interaction_reporting_urls()
                    ->try_emplace(kTestEvent, kTestInteractionUrl);
              }
              if (!top_level_seller.empty()) {
                score->set_ad_metadata(kTestAdMetadata);
                score->set_allow_component_auction(true);
                // B&A logic makes a modified bid of zero coming out of the
                // ScoreAdsReactor impossible, so this flag is for testing
                // an unreachable error case.
                if (!force_set_modified_bid_to_zero) {
                  // Normally the ScoreAdsReactor would replace a zero
                  // modified bid with the nonzero buyer bid, but we mocked
                  // it so we need to set this to a nonzero value manually.
                  score->set_bid(kNonZeroBidValue);
                }
              }
              if (client_type == CLIENT_TYPE_ANDROID) {
                score->set_ad_type(AdType::AD_TYPE_PROTECTED_AUDIENCE_AD);
              }
              if (!kanon_ghost_winners.empty()) {
                for (const auto& ghost_score : kanon_ghost_winners) {
                  *response->mutable_ghost_winning_ad_scores()->Add() =
                      ghost_score;
                }
              }
              std::move(on_done)(std::move(response),
                                 /* response_metadata= */ {});
              // Expect only one bid.
              break;
            }
            return absl::OkStatus();
          });

  // Reporting Client.
  std::unique_ptr<MockAsyncReporter> async_reporter =
      std::make_unique<MockAsyncReporter>(
          std::make_unique<MockHttpFetcherAsync>());

  // Sets up client registry
  ClientRegistry clients{scoring_signals_provider,
                         scoring_client,
                         buyer_front_end_async_client_factory_mock,
                         kv_async_client,
                         *mock_key_fetcher_manager,
                         server_component_auction_params.crypto_client,
                         std::move(async_reporter),
                         std::move(k_anon_cache_manager)};

  return {std::move(encrypted_request_with_context), std::move(clients)};
}

template <typename T>
SelectAdResponse RunReactorRequest(
    const TrustedServersConfigClient& config_client,
    const ClientRegistry& clients, const SelectAdRequest& request,
    server_common::Executor* executor, bool enable_kanon = false,
    bool enable_buyer_private_aggregate_reporting = false,
    int per_adtech_paapi_contributions_limit = 100, bool fail_fast = false,
    const ReportWinMap& report_win_map = {}) {
  metric::SfeContextMap()->Get(&request);
  grpc::CallbackServerContext context;
  SelectAdResponse response;
  T reactor(&context, &request, &response, executor, clients, config_client,
            report_win_map,
            /*enable_cancellation=*/false, enable_kanon,
            enable_buyer_private_aggregate_reporting,
            per_adtech_paapi_contributions_limit, fail_fast);
  reactor.Execute();
  return response;
}

template <bool UseKvV2ForBrowser>
void SetupScoringSignalsClient(
    const MockAsyncProvider<ScoringSignalsRequest, ScoringSignals>&
        scoring_signals_provider,
    KVAsyncClientMock& kv_async_client,
    const BuyerBidsResponseMap& expected_buyer_bids,
    const ScoringProviderMockOptions& scoring_signals_provider_options =
        ScoringProviderMockOptions{},
    const KvAsyncMockOptions& kv_async_options = KvAsyncMockOptions{}) {
  if (UseKvV2ForBrowser) {
    kv_server::v2::GetValuesResponse kv_response;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        kKvV2CompressionGroup, &kv_response));
    SetupKvAsyncClientMock(kv_async_client, kv_response, expected_buyer_bids,
                           kv_async_options);
  } else {
    SetupScoringProviderMock(scoring_signals_provider, expected_buyer_bids,
                             scoring_signals_provider_options);
  }
}

AuctionResult DecryptAppProtoAuctionResult(
    std::string& auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context);

std::pair<AuctionResult, std::string> DecryptBrowserAuctionResultAndNonce(
    std::string& auction_result_ciphertext,
    quiche::ObliviousHttpRequest::Context& context);

absl::StatusOr<std::string> UnframeAndDecompressAuctionResult(
    absl::string_view framed_response);

std::string FrameAndCompressProto(absl::string_view serialized_proto);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_SELECT_AD_REACTOR_TEST_UTILS_H_
