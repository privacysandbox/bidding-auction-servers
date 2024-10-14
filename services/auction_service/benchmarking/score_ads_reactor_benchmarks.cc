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

// Run the benchmark as follows:
// builders/tools/bazel-debian run --dynamic_mode=off -c opt --copt=-gmlt \
//   --copt=-fno-omit-frame-pointer --fission=yes --strip=never \
//   services/auction_service/benchmarking:score_ads_reactor_benchmarks -- \
//   --benchmark_time_unit=us --benchmark_repetitions=10

#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "services/auction_service/benchmarking/score_ads_no_op_logger.h"
#include "services/auction_service/reporting/reporting_helper.h"
#include "services/auction_service/score_ads_reactor.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestSecret[] = "secret";
constexpr char kTestKeyId[] = "keyid";

constexpr char kTestSellerSignals[] =
    R"json({"seller_signal": "test_seller_signals"})json";
constexpr char kTestAuctionSignals[] =
    R"json({"auction_signal": "test_auction_signals"})json";
constexpr char kTestPublisherHostname[] = "publisher_hostname";
constexpr char kTestBuyerSignals[] = "{\"test_key\":\"test_value\"}";
constexpr int kNumInterestGroups = 90;
constexpr int kNumPasAds = 90;
constexpr char kRenderUrlTemplate[] =
    "https://adtech/"
    "?adg_id=142601302539&cr_id=628073386727&cv_id=0&distinguisher_id=%d";
constexpr char kPasRenderUrlTemplate[] =
    "https://adtech/"
    "?adg_id=142601302539&cr_id=628073386727&cv_id=0&distinguisher_id=%d";
constexpr char kInterestGroupTemplate[] = "InterestGroup-%d";
constexpr char kInterestGroupOwnerTemplate[] =
    "https://interest-group-owner.com/-%d";
constexpr int kNumComponentsPerAd = 4;
constexpr char kScoringSignalKvTemplate[] = "\"%s\": [%d]";

using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;
using AdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata;
using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

class CryptoClientStub : public CryptoClientWrapperInterface {
 public:
  explicit CryptoClientStub(RawRequest* raw_request)
      : raw_request_(*raw_request) {}
  virtual ~CryptoClientStub() = default;

  // Decrypts a ciphertext using HPKE.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>
  HpkeDecrypt(const server_common::PrivateKey& private_key,
              const std::string& ciphertext) noexcept override;

  // Encrypts a plaintext payload using HPKE and the provided public key.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>
  HpkeEncrypt(const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
              const std::string& plaintext_payload) noexcept override;

  // Encrypts plaintext payload using AEAD and a secret derived from the HPKE
  // decrypt operation.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
  AeadEncrypt(const std::string& plaintext_payload,
              const std::string& secret) noexcept override;

  // Decrypts a ciphertext using AEAD and a secret derived from the HPKE
  // encrypt operation.
  absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
  AeadDecrypt(const std::string& ciphertext,
              const std::string& secret) noexcept override;

 protected:
  const RawRequest& raw_request_;
};

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>
CryptoClientStub::HpkeDecrypt(const server_common::PrivateKey& private_key,
                              const std::string& ciphertext) noexcept {
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
      hpke_decrypt_response;
  hpke_decrypt_response.set_payload(raw_request_.SerializeAsString());
  hpke_decrypt_response.set_secret(kTestSecret);
  return hpke_decrypt_response;
}

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>
CryptoClientStub::HpkeEncrypt(
    const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
    const std::string& plaintext_payload) noexcept {
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
      hpke_encrypt_response;
  hpke_encrypt_response.set_secret(kTestSecret);
  hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kTestKeyId);
  hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
      raw_request_.SerializeAsString());
  return hpke_encrypt_response;
}

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
CryptoClientStub::AeadEncrypt(const std::string& plaintext_payload,
                              const std::string& secret) noexcept {
  google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
  data.set_ciphertext(plaintext_payload);
  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
      aead_encrypt_response;
  *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
  return aead_encrypt_response;
}

absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
CryptoClientStub::AeadDecrypt(const std::string& ciphertext,
                              const std::string& secret) noexcept {
  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse
      aead_decrypt_response;
  aead_decrypt_response.set_payload(raw_request_.SerializeAsString());
  return aead_decrypt_response;
}

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
        done_callback) const {}

class V8DispatchClientStub : public V8DispatchClient {
 public:
  V8DispatchClientStub() : V8DispatchClient(dispatcher_) {}
  virtual ~V8DispatchClientStub() = default;
  absl::Status BatchExecute(std::vector<DispatchRequest>& batch,
                            BatchDispatchDoneCallback batch_callback) override;

 private:
  MockV8Dispatcher dispatcher_;
};

absl::Status V8DispatchClientStub::BatchExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback batch_callback) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;
  absl::BitGen bit_gen;
  for (const auto& request : batch) {
    DispatchResponse dispatch_response;
    if (std::strcmp(request.handler_name.c_str(),
                    kReportingDispatchHandlerFunctionName) != 0) {
      dispatch_response.resp = absl::StrFormat(
          R"JSON(
              {
                "response": {
                  "desirability":%d,
                  "bid":%f,
                  "allowComponentAuction":true
                }
              }
              )JSON",
          absl::Uniform(bit_gen, 0, 1000), absl::Uniform(bit_gen, 0, 100.0));
    } else {
      // Ignore reporting for time being.
      dispatch_response.resp = "{}";
    }
    dispatch_response.id = request.id;
    responses.emplace_back(dispatch_response);
  }
  batch_callback(responses);
  return absl::OkStatus();
}

AdWithBidMetadata BuildAdWithBid(
    int id, int number_of_component_ads = kNumComponentsPerAd) {
  AdWithBidMetadata ad_with_bid_metadata;
  const std::string render_url = absl::StrFormat(kRenderUrlTemplate, id);
  ad_with_bid_metadata.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(render_url, "arbitraryMetadataKey", 2));
  ad_with_bid_metadata.set_render(render_url);
  absl::BitGen bit_gen;
  ad_with_bid_metadata.set_bid(absl::Uniform(bit_gen, 0, 100.0));
  ad_with_bid_metadata.set_interest_group_name(
      absl::StrFormat(kInterestGroupTemplate, id));
  ad_with_bid_metadata.set_interest_group_owner(
      absl::StrFormat(kInterestGroupOwnerTemplate, id));
  for (int i = 0; i < number_of_component_ads; i++) {
    ad_with_bid_metadata.add_ad_components(
        absl::StrCat("adComponent.com/foo_components/id=", i));
  }
  ad_with_bid_metadata.set_modeling_signals(1);
  ad_with_bid_metadata.set_recency(5000);
  ad_with_bid_metadata.set_join_count(10000);
  ad_with_bid_metadata.set_ad_cost(8.123456789101);
  return ad_with_bid_metadata;
}

ProtectedAppSignalsAdWithBidMetadata BuildProtectedAppSignalsAdWithBid(int id) {
  ProtectedAppSignalsAdWithBidMetadata
      protected_app_signals_ad_with_bid_metadata;
  const std::string render_url = absl::StrFormat(kPasRenderUrlTemplate, id);
  protected_app_signals_ad_with_bid_metadata.mutable_ad()
      ->mutable_struct_value()
      ->MergeFrom(MakeAnAd(render_url, "arbitraryMetadataKey", 2));
  protected_app_signals_ad_with_bid_metadata.set_render(render_url);
  absl::BitGen bit_gen;
  protected_app_signals_ad_with_bid_metadata.set_bid(
      absl::Uniform(bit_gen, 200, 400.0));
  protected_app_signals_ad_with_bid_metadata.set_owner(
      absl::StrFormat(kInterestGroupOwnerTemplate, id));
  return protected_app_signals_ad_with_bid_metadata;
}

std::vector<AdWithBidMetadata> BuildAdWithBids(
    int num_interest_groups = kNumInterestGroups) {
  std::vector<AdWithBidMetadata> ads_with_bid_metadata;
  ads_with_bid_metadata.reserve(num_interest_groups);
  for (int i = 0; i < num_interest_groups; ++i) {
    ads_with_bid_metadata.emplace_back(BuildAdWithBid(i, kNumComponentsPerAd));
  }
  return ads_with_bid_metadata;
}

std::vector<ProtectedAppSignalsAdWithBidMetadata>
BuildProtectedAppSignalsAdWithBids(int num_pas_ads = kNumPasAds) {
  std::vector<ProtectedAppSignalsAdWithBidMetadata>
      protected_app_signals_ads_with_bid_metadata;
  protected_app_signals_ads_with_bid_metadata.reserve(num_pas_ads);
  for (int i = 0; i < num_pas_ads; ++i) {
    protected_app_signals_ads_with_bid_metadata.emplace_back(
        BuildProtectedAppSignalsAdWithBid(i));
  }
  return protected_app_signals_ads_with_bid_metadata;
}

std::string BuildScoringSignals(int num_interest_groups = kNumInterestGroups,
                                int num_pas_ads = kNumPasAds) {
  std::vector<std::string> scoring_signals;
  absl::BitGen bit_gen;
  scoring_signals.reserve(num_interest_groups + num_pas_ads);
  for (int i = 0; i < num_interest_groups; ++i) {
    scoring_signals.emplace_back(absl::StrFormat(
        kScoringSignalKvTemplate, absl::StrFormat(kRenderUrlTemplate, i),
        absl::Uniform(bit_gen, 0, 1000)));
  }
  for (int i = 0; i < num_pas_ads; ++i) {
    scoring_signals.emplace_back(absl::StrFormat(
        kScoringSignalKvTemplate, absl::StrFormat(kPasRenderUrlTemplate, i),
        absl::Uniform(bit_gen, 0, 1000)));
  }
  return absl::StrCat("{\"renderUrls\": {", absl::StrJoin(scoring_signals, ","),
                      "}}");
}

RawRequest BuildRawRequest(const std::vector<AdWithBidMetadata>& ads_with_bids,
                           const std::string& seller_signals,
                           const std::string& auction_signals,
                           const std::string& scoring_signals,
                           const std::string& publisher_hostname) {
  RawRequest raw_request;
  for (int i = 0; i < ads_with_bids.size(); i++) {
    raw_request.mutable_per_buyer_signals()->try_emplace(
        ads_with_bids[i].interest_group_owner(), kTestBuyerSignals);
    *raw_request.add_ad_bids() = ads_with_bids[i];
  }
  raw_request.set_seller_signals(seller_signals);
  raw_request.set_auction_signals(auction_signals);
  raw_request.set_scoring_signals(scoring_signals);
  raw_request.set_publisher_hostname(publisher_hostname);

  return raw_request;
}

RawRequest BuildRawRequest(
    const std::vector<AdWithBidMetadata>& ads_with_bids,
    const std::vector<ProtectedAppSignalsAdWithBidMetadata>&
        protected_app_signals_ad_with_bids,
    const std::string& seller_signals, const std::string& auction_signals,
    const std::string& scoring_signals, const std::string& publisher_hostname) {
  RawRequest raw_request =
      BuildRawRequest(ads_with_bids, seller_signals, auction_signals,
                      scoring_signals, publisher_hostname);
  for (int i = 0; i < protected_app_signals_ad_with_bids.size(); i++) {
    raw_request.mutable_per_buyer_signals()->try_emplace(
        protected_app_signals_ad_with_bids[i].owner(), kTestBuyerSignals);
    *raw_request.add_protected_app_signals_ad_bids() =
        protected_app_signals_ad_with_bids[i];
  }

  return raw_request;
}

static void BM_ScoreAdsProtectedAudience(benchmark::State& state) {
  // Setup.
  CommonTestInit();
  ScoreAdsRequest score_ads_request;
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);

  ScoreAdsRequest::ScoreAdsRawRequest score_ads_raw_request = BuildRawRequest(
      BuildAdWithBids(), kTestSellerSignals, kTestAuctionSignals,
      BuildScoringSignals(), kTestPublisherHostname);
  *score_ads_request.mutable_request_ciphertext() =
      score_ads_raw_request.SerializeAsString();

  auto async_reporter = std::make_unique<AsyncReporterStub>(
      /*http_fetcher_async=*/nullptr);
  auto crypto_client = CryptoClientStub(&score_ads_raw_request);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true,
      .enable_adtech_code_logging = false,
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_report_win_input_noising = true};
  score_ads_request.set_request_ciphertext(
      score_ads_raw_request.SerializeAsString());
  score_ads_request.set_key_id(kTestKeyId);

  ScoreAdsResponse response;
  V8DispatchClientStub dispatcher;
  grpc::CallbackServerContext context;
  for (auto _ : state) {
    // This code gets timed.
    metric::MetricContextMap<ScoreAdsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&score_ads_request);
    ScoreAdsReactor reactor(&context, dispatcher, &score_ads_request, &response,
                            std::make_unique<ScoreAdsNoOpLogger>(),
                            key_fetcher_manager.get(), &crypto_client,
                            async_reporter.get(), runtime_config);
    reactor.Execute();
  }
}

static void BM_ScoreAdsProtectedAudienceAndAppSignals(benchmark::State& state) {
  // Setup.
  CommonTestInit();
  ScoreAdsRequest score_ads_request;
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);

  ScoreAdsRequest::ScoreAdsRawRequest score_ads_raw_request =
      BuildRawRequest(BuildAdWithBids(), BuildProtectedAppSignalsAdWithBids(),
                      kTestSellerSignals, kTestAuctionSignals,
                      BuildScoringSignals(), kTestPublisherHostname);
  *score_ads_request.mutable_request_ciphertext() =
      score_ads_raw_request.SerializeAsString();

  auto async_reporter = std::make_unique<AsyncReporterStub>(
      /*http_fetcher_async=*/nullptr);
  auto crypto_client = CryptoClientStub(&score_ads_raw_request);
  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);
  AuctionServiceRuntimeConfig runtime_config = {
      .enable_seller_debug_url_generation = true,
      .enable_adtech_code_logging = true,
      .enable_report_result_url_generation = true,
      .enable_report_win_url_generation = true,
      .enable_protected_app_signals = true,
      .enable_report_win_input_noising = true};
  score_ads_request.set_request_ciphertext(
      score_ads_raw_request.SerializeAsString());
  score_ads_request.set_key_id(kTestKeyId);

  ScoreAdsResponse response;
  V8DispatchClientStub dispatcher;
  grpc::CallbackServerContext context;
  for (auto _ : state) {
    // This code gets timed.
    metric::MetricContextMap<ScoreAdsRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&score_ads_request);
    ScoreAdsReactor reactor(&context, dispatcher, &score_ads_request, &response,
                            std::make_unique<ScoreAdsNoOpLogger>(),
                            key_fetcher_manager.get(), &crypto_client,
                            async_reporter.get(), runtime_config);
    reactor.Execute();
  }
}

// Register the function as a benchmark
BENCHMARK(BM_ScoreAdsProtectedAudience);
BENCHMARK(BM_ScoreAdsProtectedAudienceAndAppSignals);

// Run the benchmark
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
