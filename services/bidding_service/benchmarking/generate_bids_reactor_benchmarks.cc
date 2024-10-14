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
//   services/bidding_service/benchmarking:generate_bids_reactor_benchmarks -- \
//   --benchmark_time_unit=us --benchmark_repetitions=10

#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/metric/server_definition.h"
#include "services/common/reporters/async_reporter.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestSecret[] = "secret";
constexpr char kTestKeyId[] = "keyid";
constexpr char kTestInterestGroupNameTemplate[] = "test_interest_group_%d";
constexpr char kTrustedBiddingSignalsKey[] = "trusted_bidding_signals_key";
constexpr char kStructKeyTemplate[] = "structKey_%d";
constexpr char kStructValueTemplate[] = "structValue_%d";
constexpr char kNumInterestGroupsForBidding = 50;
constexpr int kNumAdRenderIds = 20;
constexpr int kNumSignalsFields = 20;

using RawRequest = GenerateBidsRequest::GenerateBidsRawRequest;

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
  MockV8DispatchClient dispatcher_;
};

absl::Status V8DispatchClientStub::BatchExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback batch_callback) {
  std::vector<absl::StatusOr<DispatchResponse>> responses;
  absl::BitGen bit_gen;
  for (const auto& request : batch) {
    DispatchResponse dispatch_response;
    dispatch_response.resp = absl::StrFormat(
        R"JSON({
            "render": "fake_url",
            "bid": %f
          })JSON",
        absl::Uniform(bit_gen, 0, 100.0));
    dispatch_response.id = request.id;
    responses.emplace_back(dispatch_response);
  }
  batch_callback(responses);
  return absl::OkStatus();
}

InterestGroupForBidding GetInterestGroupForBidding(const std::string& name) {
  InterestGroupForBidding ig_for_bidding;
  ig_for_bidding.set_name(name);
  ig_for_bidding.mutable_trusted_bidding_signals_keys()->Add(
      kTrustedBiddingSignalsKey);
  ig_for_bidding.set_trusted_bidding_signals(
      MakeTrustedBiddingSignalsForIG(ig_for_bidding));
  ig_for_bidding.set_user_bidding_signals(
      R"JSON({"years": [1776, 1868], "name": "winston", "someId": 1789})JSON");
  for (int i = 0; i < kNumAdRenderIds; ++i) {
    *ig_for_bidding.mutable_ad_render_ids()->Add() =
        absl::StrCat("ad_render_id_", i);
  }
  ig_for_bidding.mutable_android_signals();
  return ig_for_bidding;
}

// These are borrowed from the random module and randomness has been removed
// since we want to run the load deterministically.
google::protobuf::Struct MakeAJsonStruct(int num_fields) {
  google::protobuf::Struct out_struct;
  auto& struct_fields = *out_struct.mutable_fields();
  for (int i = 0; i < num_fields; ++i) {
    struct_fields[absl::StrFormat(kStructKeyTemplate, i)].set_string_value(
        absl::StrFormat(kStructValueTemplate, i));
  }
  return out_struct;
}

std::string MakeAStructJsonString(int num_fields = kNumSignalsFields) {
  std::string json_output;
  auto fields = MakeAJsonStruct(num_fields);
  CHECK_OK(ProtoToJson(fields, &json_output));
  return json_output;
}

GenerateBidsRequest::GenerateBidsRawRequest
GetGenerateBidsRawRequestForAndroid() {
  GenerateBidsRequest::GenerateBidsRawRequest raw_request;
  for (int i = 0; i < kNumInterestGroupsForBidding; ++i) {
    *raw_request.mutable_interest_group_for_bidding()->Add() =
        GetInterestGroupForBidding(
            absl::StrFormat(kTestInterestGroupNameTemplate, i));
  }
  std::string signals = MakeAStructJsonString();
  raw_request.set_auction_signals(signals);
  raw_request.set_buyer_signals(std::move(signals));
  return raw_request;
}

static void BM_ProtectedAudience(benchmark::State& state) {
  // Setup.
  CommonTestInit();
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);

  TrustedServersConfigClient config_client({});
  config_client.SetOverride(kTrue, TEST_MODE);
  auto key_fetcher_manager =
      CreateKeyFetcherManager(config_client, /*public_key_fetcher=*/nullptr);

  GenerateBidsRequest request;
  request.set_key_id(kTestKeyId);
  auto raw_request = GetGenerateBidsRawRequestForAndroid();
  request.set_request_ciphertext(raw_request.SerializeAsString());
  auto crypto_client = CryptoClientStub(&raw_request);

  GenerateBidsResponse response;
  V8DispatchClientStub dispatcher;
  grpc::CallbackServerContext context;
  BiddingServiceRuntimeConfig runtime_config = {
      .enable_buyer_debug_url_generation = true};
  for (auto _ : state) {
    // This code gets timed.
    metric::MetricContextMap<google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request);
    GenerateBidsReactor reactor(&context, dispatcher, &request, &response,
                                std::make_unique<BiddingNoOpLogger>(),
                                key_fetcher_manager.get(), &crypto_client,
                                runtime_config);
    reactor.Execute();
  }
}

// Register the function as a benchmark
BENCHMARK(BM_ProtectedAudience);

// Run the benchmark
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
