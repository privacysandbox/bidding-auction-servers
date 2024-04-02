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

#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/metric/server_definition.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/test/random.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "services/seller_frontend_service/util/select_ad_reactor_test_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kDebugUrlLength = 500;
constexpr char kBaseAdRenderUrl[] = "ads.barbecue.com";
constexpr char kInterestGroupName[] = "meat_lovers";
constexpr char kEurIsoCode[] = "EUR";
constexpr char kUsdIsoCode[] = "USD";

enum class BuyerMockType { DEBUG_REPORTING, BID_CURRENCY };

class AsyncReporterStub : public AsyncReporter {
 public:
  explicit AsyncReporterStub(
      std::unique_ptr<HttpFetcherAsync> http_fetcher_async)
      : AsyncReporter(std::move(http_fetcher_async)) {}
  virtual ~AsyncReporterStub() = default;

  void DoReport(const HTTPRequest& reporting_request,
                absl::AnyInvocable<void(absl::StatusOr<absl::string_view>) &&>
                    done_callback) const override;
};

void AsyncReporterStub::DoReport(
    const HTTPRequest& reporting_request,
    absl::AnyInvocable<void(absl::StatusOr<absl::string_view>) &&>
        done_callback) const {
  PS_VLOG(1) << "Mocking success for reporting to URL: "
             << reporting_request.url << ", body: " << reporting_request.body;
  std::move(done_callback)("success");
}

class BuyerFrontEndAsyncClientStub : public BuyerFrontEndAsyncClient {
 public:
  explicit BuyerFrontEndAsyncClientStub(const std::string& ad_render_url,
                                        const BuyerMockType buyer_mock_type);

  absl::Status Execute(
      std::unique_ptr<GetBidsRequest> request, const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<GetBidsResponse>>) &&>
          on_done,
      absl::Duration timeout) const override;

  // Executes the inter service GRPC request asynchronously.
  absl::Status ExecuteInternal(
      std::unique_ptr<GetBidsRequest::GetBidsRawRequest> request,
      const RequestMetadata& metadata,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                  GetBidsResponse::GetBidsRawResponse>>) &&>
          on_done,
      absl::Duration timeout) const override;

 private:
  const std::string ad_render_url_;
  const BuyerMockType buyer_mock_type_;
};

BuyerFrontEndAsyncClientStub::BuyerFrontEndAsyncClientStub(
    const std::string& ad_render_url, const BuyerMockType buyer_mock_type)
    : ad_render_url_(ad_render_url), buyer_mock_type_(buyer_mock_type) {}

absl::Status BuyerFrontEndAsyncClientStub::Execute(
    std::unique_ptr<GetBidsRequest> request, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<GetBidsResponse>>) &&>
        on_done,
    absl::Duration timeout) const {
  return absl::NotFoundError("Not implemented");
}

absl::Status BuyerFrontEndAsyncClientStub::ExecuteInternal(
    std::unique_ptr<GetBidsRequest::GetBidsRawRequest> request,
    const RequestMetadata& metadata,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>) &&>
        on_done,
    absl::Duration timeout) const {
  auto response = std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  switch (buyer_mock_type_) {
    case BuyerMockType::DEBUG_REPORTING: {
      AdWithBid bid =
          BuildNewAdWithBid(ad_render_url_, "testIG", /*bid_value=*/1.0,
                            /*enable_event_level_debug_reporting=*/true);
      auto* debug_report_urls = bid.mutable_debug_report_urls();
      *debug_report_urls->mutable_auction_debug_loss_url() =
          std::string(kDebugUrlLength, 'C');
      *debug_report_urls->mutable_auction_debug_win_url() =
          std::string(kDebugUrlLength, 'D');
      response->mutable_bids()->Add(std::move(bid));
      break;
    }
    case BuyerMockType::BID_CURRENCY: {
      std::vector<AdWithBid> ads_with_bids = GetAdWithBidsInMultipleCurrencies(
          /*num_ad_with_bids=*/40, /*num_mismatched=*/15,
          /*matching_currency=*/kUsdIsoCode,
          /*mismatching_currency=*/kEurIsoCode, kBaseAdRenderUrl,
          kInterestGroupName);
      for (const auto& ad_with_bid : ads_with_bids) {
        // Add all AwBs so all are returned by mock.
        response->mutable_bids()->Add()->CopyFrom(ad_with_bid);
      }
      break;
    }
  }
  std::move(on_done)(std::move(response));
  return absl::OkStatus();
}

class ScoringClientStub
    : public AsyncClient<ScoreAdsRequest, ScoreAdsResponse,
                         ScoreAdsRequest::ScoreAdsRawRequest,
                         ScoreAdsResponse::ScoreAdsRawResponse> {
 public:
  absl::Status Execute(
      std::unique_ptr<ScoreAdsRequest> request, const RequestMetadata& metadata,
      absl::AnyInvocable<
          void(absl::StatusOr<std::unique_ptr<ScoreAdsResponse>>) &&>
          on_done,
      absl::Duration timeout) const override;

  absl::Status ExecuteInternal(
      std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
      const RequestMetadata& metadata,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                  ScoreAdsResponse::ScoreAdsRawResponse>>) &&>
          on_done,
      absl::Duration timeout) const override;
};

absl::Status ScoringClientStub::Execute(
    std::unique_ptr<ScoreAdsRequest> request, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<ScoreAdsResponse>>) &&>
        on_done,
    absl::Duration timeout) const {
  return absl::OkStatus();
}

absl::Status ScoringClientStub::ExecuteInternal(
    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> request,
    const RequestMetadata& metadata,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                ScoreAdsResponse::ScoreAdsRawResponse>>) &&>
        on_done,
    absl::Duration timeout) const {
  ScoreAdsResponse::ScoreAdsRawResponse response;
  float i = 1;
  // Last bid wins.
  for (const auto& bid : request->ad_bids()) {
    ScoreAdsResponse::AdScore score;
    score.set_render(bid.render());
    score.mutable_component_renders()->CopyFrom(bid.ad_components());
    score.set_desirability(i++);
    score.set_buyer_bid(bid.bid());
    score.set_interest_group_name(bid.interest_group_name());
    score.set_interest_group_owner(bid.interest_group_owner());
    *response.mutable_ad_score() = score;
    auto* debug_report_urls = score.mutable_debug_report_urls();
    *debug_report_urls->mutable_auction_debug_loss_url() =
        std::string(kDebugUrlLength, 'A');
    *debug_report_urls->mutable_auction_debug_win_url() =
        std::string(kDebugUrlLength, 'B');
  }
  std::move(on_done)(
      std::make_unique<ScoreAdsResponse::ScoreAdsRawResponse>(response));
  return absl::OkStatus();
}

class BuyerFrontEndAsyncClientFactoryStub
    : public ClientFactory<BuyerFrontEndAsyncClient, absl::string_view> {
 public:
  BuyerFrontEndAsyncClientFactoryStub(
      const SelectAdRequest& request,
      const ProtectedAuctionInput& protected_auction_input,
      const BuyerMockType buyer_mock_type);
  std::shared_ptr<const BuyerFrontEndAsyncClient> Get(
      absl::string_view client_key) const override;

 private:
  const SelectAdRequest& request_;
  absl::flat_hash_map<std::string,
                      std::shared_ptr<BuyerFrontEndAsyncClientStub>>
      buyer_clients_;
};

BuyerFrontEndAsyncClientFactoryStub::BuyerFrontEndAsyncClientFactoryStub(
    const SelectAdRequest& request,
    const ProtectedAuctionInput& protected_auction_input,
    const BuyerMockType buyer_mock_type)
    : request_(request) {
  ErrorAccumulator error_accumulator;
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);
  for (const auto& buyer_ig_owner : request_.auction_config().buyer_list()) {
    buyer_clients_.emplace(
        buyer_ig_owner,
        std::make_shared<BuyerFrontEndAsyncClientStub>(
            buyer_to_ad_url.at(buyer_ig_owner), buyer_mock_type));
  }
}

std::shared_ptr<const BuyerFrontEndAsyncClient>
BuyerFrontEndAsyncClientFactoryStub::Get(absl::string_view client_key) const {
  return buyer_clients_.at(client_key);
}

class KeyFetcherManagerStub : public server_common::KeyFetcherManagerInterface {
 public:
  // Fetches a public key to be used for encrypting outgoing requests.
  absl::StatusOr<google::cmrt::sdk::public_key_service::v1::PublicKey>
  GetPublicKey(server_common::CloudPlatform cloud_platform) noexcept override;

  // Fetches the corresponding private key for a public key ID.
  std::optional<server_common::PrivateKey> GetPrivateKey(
      const google::scp::cpio::PublicPrivateKeyPairId& key_id) noexcept
      override;

  // Queues key refresh jobs on the class' executor as often as defined by
  // 'key_refresh_period'.
  void Start() noexcept override;
};

absl::StatusOr<google::cmrt::sdk::public_key_service::v1::PublicKey>
KeyFetcherManagerStub::GetPublicKey(
    server_common::CloudPlatform cloud_platform) noexcept {
  return google::cmrt::sdk::public_key_service::v1::PublicKey{};
}

// Fetches the corresponding private key for a public key ID.
std::optional<server_common::PrivateKey> KeyFetcherManagerStub::GetPrivateKey(
    const google::scp::cpio::PublicPrivateKeyPairId& unused) noexcept {
  server_common::PrivateKey private_key;
  HpkeKeyset keyset;
  private_key.key_id = std::to_string(keyset.key_id);
  private_key.private_key = GetHpkePrivateKey(keyset.private_key);
  return private_key;
}

// Queues key refresh jobs on the class' executor as often as defined by
// 'key_refresh_period'.
void KeyFetcherManagerStub::Start() noexcept {}

class ScoringSignalsProviderStub
    : public AsyncProvider<ScoringSignalsRequest, ScoringSignals> {
 public:
  explicit ScoringSignalsProviderStub(const SelectAdRequest& request);
  void Get(
      const ScoringSignalsRequest& params,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<ScoringSignals>>,
                              GetByteSize) &&>
          on_done,
      absl::Duration timeout) const override;

 private:
  const SelectAdRequest& request_;
  std::string scoring_signals_;
};

ScoringSignalsProviderStub::ScoringSignalsProviderStub(
    const SelectAdRequest& request)
    : request_(request) {
  absl::flat_hash_map<std::string, std::string> buyer_to_ad_url =
      BuildBuyerWinningAdUrlMap(request_);
  scoring_signals_ = R"({"renderUrls": {)";
  std::vector<std::string> key_vals;
  for (const auto& [buyer, url] : buyer_to_ad_url) {
    key_vals.push_back(absl::StrFormat(R"("%s" : ["1"])", url));
  }
  scoring_signals_.append(absl::StrJoin(key_vals, ", "));
  scoring_signals_.append("}");
}

void ScoringSignalsProviderStub::Get(
    const ScoringSignalsRequest& params,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<ScoringSignals>>,
                            GetByteSize) &&>
        on_done,
    absl::Duration timeout) const {
  std::move(on_done)(
      std::make_unique<ScoringSignals>(ScoringSignals{
          .scoring_signals = std::make_unique<std::string>(scoring_signals_)}),
      {0, scoring_signals_.size()});
}

static void BM_PerformDebugReporting(benchmark::State& state) {
  ProtectedAuctionInput protected_auction_input =
      MakeARandomProtectedAuctionInput<ProtectedAuctionInput>();
  protected_auction_input.set_enable_debug_reporting(true);
  SelectAdRequest request = MakeARandomSelectAdRequest<ProtectedAuctionInput>(
      kSellerOriginDomain, protected_auction_input);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext<ProtectedAuctionInput>(
          protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);
  auto context = std::make_unique<quiche::ObliviousHttpRequest::Context>(
      std::move(encryption_context));
  ScoringClientStub scoring_client;

  // Scoring signal provider
  ScoringSignalsProviderStub scoring_provider(request);

  auto async_reporter = std::make_unique<AsyncReporterStub>(
      std::make_unique<MockHttpFetcherAsync>());
  BuyerFrontEndAsyncClientFactoryStub buyer_clients(
      request, protected_auction_input, BuyerMockType::DEBUG_REPORTING);
  KeyFetcherManagerStub key_fetcher_manager;
  TrustedServersConfigClient config_client = CreateConfig();
  config_client.SetFlagForTest("", CONSENTED_DEBUG_TOKEN);
  config_client.SetFlagForTest(kFalse, ENABLE_PROTECTED_APP_SIGNALS);
  ClientRegistry clients{scoring_provider,
                         scoring_client,
                         buyer_clients,
                         key_fetcher_manager,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::OFF);

  for (auto _ : state) {
    // This code gets timed.
    grpc::CallbackServerContext context;
    SelectAdResponse response;
    metric::MetricContextMap<SelectAdRequest>(
        server_common::telemetry::BuildDependentConfig(config_proto))
        ->Get(&request);
    SelectAdReactorForWeb reactor(&context, &request, &response, clients,
                                  config_client);
    reactor.Execute();
  }
}

BENCHMARK(BM_PerformDebugReporting);

static void BM_PerformCurrencyCheckingAndFiltering(benchmark::State& state) {
  ProtectedAuctionInput protected_auction_input =
      MakeARandomProtectedAuctionInput<ProtectedAuctionInput>();
  SelectAdRequest request = MakeARandomSelectAdRequest<ProtectedAuctionInput>(
      kSellerOriginDomain, protected_auction_input, /*set_buyer_egid=*/false,
      /*set_seller_egid=*/false, /*seller_currency=*/kUsdIsoCode,
      /*buyer_currency=*/kEurIsoCode);
  auto [encrypted_protected_auction_input, encryption_context] =
      GetCborEncodedEncryptedInputAndOhttpContext<ProtectedAuctionInput>(
          protected_auction_input);
  *request.mutable_protected_auction_ciphertext() =
      std::move(encrypted_protected_auction_input);
  auto context = std::make_unique<quiche::ObliviousHttpRequest::Context>(
      std::move(encryption_context));
  ScoringClientStub scoring_client;

  // Scoring signal provider
  ScoringSignalsProviderStub scoring_provider(request);

  auto async_reporter = std::make_unique<AsyncReporterStub>(
      std::make_unique<MockHttpFetcherAsync>());
  BuyerFrontEndAsyncClientFactoryStub buyer_clients(
      request, protected_auction_input, BuyerMockType::BID_CURRENCY);
  KeyFetcherManagerStub key_fetcher_manager;
  TrustedServersConfigClient config_client = CreateConfig();
  config_client.SetFlagForTest("", CONSENTED_DEBUG_TOKEN);
  config_client.SetFlagForTest(kTrue, ENABLE_PROTECTED_APP_SIGNALS);
  ClientRegistry clients{scoring_provider,
                         scoring_client,
                         buyer_clients,
                         key_fetcher_manager,
                         /* crypto_client = */ nullptr,
                         std::move(async_reporter)};

  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::OFF);

  for (auto _ : state) {
    // This code gets timed.
    grpc::CallbackServerContext context;
    SelectAdResponse response;
    metric::MetricContextMap<SelectAdRequest>(
        server_common::telemetry::BuildDependentConfig(config_proto))
        ->Get(&request);
    SelectAdReactorForWeb reactor(&context, &request, &response, clients,
                                  config_client);
    reactor.Execute();
  }
}

BENCHMARK(BM_PerformCurrencyCheckingAndFiltering);

BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
