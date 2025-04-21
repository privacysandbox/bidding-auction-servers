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

#include "benchmark/benchmark.h"
#include "services/bidding_service/benchmarking/bidding_no_op_logger.h"
#include "services/bidding_service/benchmarking/generate_bids_reactor_benchmarks_util.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {

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

}  // namespace privacy_sandbox::bidding_auction_servers

// Register the function as a benchmark
BENCHMARK(privacy_sandbox::bidding_auction_servers::BM_ProtectedAudience);

// Run the benchmark
BENCHMARK_MAIN();
