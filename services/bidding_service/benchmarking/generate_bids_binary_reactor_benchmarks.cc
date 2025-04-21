// Copyright 2024 Google LLC
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
//   services/bidding_service/benchmarking:generate_bids_binary_reactor_benchmarks
//   -- --benchmark_time_unit=us --benchmark_repetitions=10

#include "benchmark/benchmark.h"
#include "services/bidding_service/benchmarking/generate_bids_reactor_benchmarks_util.h"
#include "services/bidding_service/generate_bids_binary_reactor.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {

class GenerateBidByobDispatchClientStub
    : public ByobDispatchClient<
          roma_service::GenerateProtectedAudienceBidRequest,
          roma_service::GenerateProtectedAudienceBidResponse> {
 public:
  ~GenerateBidByobDispatchClientStub() override = default;
  absl::Status LoadSync(std::string version, std::string code) override;
  absl::Status Execute(
      const roma_service::GenerateProtectedAudienceBidRequest& request,
      absl::Duration timeout,
      absl::AnyInvocable<
          void(absl::StatusOr<
               roma_service::GenerateProtectedAudienceBidResponse>) &&>
          callback) override;
};

absl::Status GenerateBidByobDispatchClientStub::LoadSync(std::string version,
                                                         std::string code) {
  return absl::OkStatus();
}

absl::Status GenerateBidByobDispatchClientStub::Execute(
    const roma_service::GenerateProtectedAudienceBidRequest& request,
    absl::Duration timeout,
    absl::AnyInvocable<
        void(absl::StatusOr<
             roma_service::GenerateProtectedAudienceBidResponse>) &&>
        callback) {
  roma_service::GenerateProtectedAudienceBidResponse bid_response;
  *bid_response.add_bids() = MakeARandomRomaProtectedAudienceBid(
      ToUnixNanos(absl::Now()), /*debug_reporting_enabled=*/true,
      /*allow_component_auction=*/true);
  std::move(callback)(bid_response);
  return absl::OkStatus();
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
  GenerateBidByobDispatchClientStub byob_client;
  grpc::CallbackServerContext context;
  BiddingServiceRuntimeConfig runtime_config = {
      .enable_buyer_debug_url_generation = true};
  for (auto _ : state) {
    // This code gets timed.
    metric::MetricContextMap<google::protobuf::Message>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto))
        ->Get(&request);
    GenerateBidsBinaryReactor reactor(&context, byob_client, &request,
                                      &response, key_fetcher_manager.get(),
                                      &crypto_client, runtime_config);
    reactor.Execute();
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers

// Register the function as a benchmark
BENCHMARK(privacy_sandbox::bidding_auction_servers::BM_ProtectedAudience);

// Run the benchmark
BENCHMARK_MAIN();
