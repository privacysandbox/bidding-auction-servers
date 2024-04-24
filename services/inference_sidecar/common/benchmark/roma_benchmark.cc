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

// This benchmark measures the performance of the Roma used to run inference.
//
// Run the benchmark as follows:
// builders/tools/bazel-debian run //benchmark:roma_benchmark -- \
//   --benchmark_counters_tabular=true \
//   --benchmark_repetitions=5 \
//   --benchmark_min_warmup_time=1 \
//   2>/dev/null \
//   | grep -Ev "sandbox.cc|monitor_base.cc|sandbox2.cc" \
// > /tmp/report.txt
//
// How to read the benchmark results:
//
// Benchmark name:
// * `process_time/real_time`: Measures the wall time for latency and CPU time
//   of all threads not just limited to the main thread.
// * `threads:8`: The number of threads concurrently executing the benchmark.
//
// Metrics:
// * `Time`: The total elapsed wall clock time per Iteration.
// * `CPU`: The total CPU time spent by all the threads per Iteration.
// * `Iterations`: The number of serial executions.
// * `Throughput` & `items_per_second`: The number of Iterations per second.
// * `Latency`: Average time spent per Iteration.
//
// This Roma bechmark code is re-written based on:
// https://github.com/privacysandbox/data-plane-shared-libraries/blob/main/src/roma/benchmark/ba_server_benchmark.cc

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "benchmark/benchmark.h"
#include "proto/inference_sidecar.pb.h"
#include "src/roma/config/config.h"
#include "src/roma/interface/roma.h"
#include "src/roma/roma_service/roma_service.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using LoadRequest = ::google::scp::roma::CodeObject;
using DispatchRequest = ::google::scp::roma::InvocationSharedRequest<>;
using DispatchConfig = ::google::scp::roma::Config<>;
using LoadResponse = ::google::scp::roma::ResponseObject;
using DispatchResponse = ::google::scp::roma::ResponseObject;

constexpr absl::string_view kVersionString = "v1";
constexpr absl::string_view kCode = R"(
  function run() {
    console.log("Hello World!");
  }
)";
// TODO(b/330364610): Use the real inference requests.
constexpr absl::string_view kInferenceCode = R"(
  function run() {
    console.log("Hello Inference!");
    const batchInferenceRequest = {
        request: [
            {
                model_path: '/model/path',
                tensors: [
                    {
                        tensor_name: 'input',
                        data_type: 'INT32',
                        tensor_shape: [1, 1],
                        tensor_content: ['8'],
                    },
                ],
            },
        ],
    };

    const jsonRequest = JSON.stringify(batchInferenceRequest);
    const output = runInference(jsonRequest);
    return output;
  }
)";

constexpr absl::string_view kHandlerName = "run";
// TODO(b/330364610): Run the benchmark with variable #Roma workers.
constexpr int kNumRomaWorkers = 16;
// TODO(b/330364610): Enable multi threaded benchmark.
constexpr int kMaxThreads = 1;

static void ExportMetrics(benchmark::State& state) {
  state.SetItemsProcessed(state.iterations());

  state.counters["Throughput"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.counters["Latency"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate |
                                                 benchmark::Counter::kInvert);
}

void TestRunInference(google::scp::roma::FunctionBindingPayload<>& wrapper) {
  const std::string& payload = wrapper.io_proto.input_string();

  PredictRequest predict_request;
  predict_request.set_input(payload);

  // No gRPC to the inference sidecar.
  PredictResponse response;
  response.set_output("0.57721");
  wrapper.io_proto.set_output_string(response.output());
}

class RomaFixture : public benchmark::Fixture {
 public:
  void SetUp(::benchmark::State& state) {
    if (state.thread_index() != 0) return;

    DispatchConfig config;
    config.number_of_workers = kNumRomaWorkers;

    // Registers runInference().
    auto run_inference_function_object =
        std::make_unique<google::scp::roma::FunctionBindingObjectV2<>>();
    run_inference_function_object->function_name = std::string("runInference");
    run_inference_function_object->function = TestRunInference;
    config.RegisterFunctionBinding(std::move(run_inference_function_object));

    roma_service_ = std::make_unique<
        google::scp::roma::sandbox::roma_service::RomaService<>>(
        std::move(config));
    CHECK(roma_service_->Init().ok());
  }
  void TearDown(::benchmark::State& state) {
    if (state.thread_index() != 0) return;

    CHECK(roma_service_->Stop().ok());
    roma_service_.reset();
  }

  // Loads the JS code synchronously.
  // TODO(b/330364610): Re-use the B&A V8Dispatcher class.
  void LoadCode(absl::string_view version, absl::string_view js_code) const {
    auto request = std::make_unique<LoadRequest>(LoadRequest{
        .version_string = std::string(version),
        .js = std::string(js_code),
    });

    absl::Notification load_finished;
    absl::Status load_status;

    if (absl::Status try_load = roma_service_->LoadCodeObj(
            std::move(request),
            [&load_finished,
             &load_status](absl::StatusOr<LoadResponse> res) {  // NOLINT
              if (!res.ok()) {
                load_status.Update(res.status());
              }
              load_finished.Notify();
            });
        !try_load.ok()) {
      // Load callback won't be called, we can return.
      return;
    }
    load_finished.WaitForNotification();
    CHECK(load_status.ok());
  }

  // Executes the JS code synchronously.
  void BatchExecute(std::vector<DispatchRequest>& batch) const {
    absl::Notification notification;
    auto batch_callback =
        [&notification](
            const std::vector<absl::StatusOr<DispatchResponse>>& result) {
          for (const auto& status_or : result) {
            CHECK(status_or.ok());
          }
          notification.Notify();
        };

    // This call schedules the code to be executed:
    CHECK(roma_service_->BatchExecute(batch, std::move(batch_callback)).ok());
    notification.WaitForNotification();
  }

  void BatchExecute(absl::string_view handler_name, int batch_size) {
    DispatchRequest request = {
        .id = "id",
        .version_string = std::string(kVersionString),
        .handler_name = std::string{handler_name},
    };

    std::vector<DispatchRequest> batch(batch_size, request);
    BatchExecute(batch);
  }

 protected:
  std::unique_ptr<google::scp::roma::sandbox::roma_service::RomaService<>>
      roma_service_;
};

BENCHMARK_DEFINE_F(RomaFixture, BM_LoadCode)(benchmark::State& state) {
  for (auto _ : state) {
    LoadCode(kVersionString, kCode);
  }
  ExportMetrics(state);
}

BENCHMARK_DEFINE_F(RomaFixture, BM_LoadCode_Inference)
(benchmark::State& state) {
  for (auto _ : state) {
    LoadCode(kVersionString, kInferenceCode);
  }
  ExportMetrics(state);
}

BENCHMARK_DEFINE_F(RomaFixture, BM_BatchExecute)(benchmark::State& state) {
  LoadCode(kVersionString, kCode);
  for (auto _ : state) {
    BatchExecute(kHandlerName, kNumRomaWorkers);
  }
  ExportMetrics(state);
}

BENCHMARK_DEFINE_F(RomaFixture, BM_BatchExecute_Inference)
(benchmark::State& state) {
  LoadCode(kVersionString, kInferenceCode);
  for (auto _ : state) {
    BatchExecute(kHandlerName, kNumRomaWorkers);
  }
  ExportMetrics(state);
}

// Registers the functions to the benchmark.
BENCHMARK_REGISTER_F(RomaFixture, BM_LoadCode)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

BENCHMARK_REGISTER_F(RomaFixture, BM_LoadCode_Inference)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

BENCHMARK_REGISTER_F(RomaFixture, BM_BatchExecute)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

BENCHMARK_REGISTER_F(RomaFixture, BM_BatchExecute_Inference)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

// Runs the benchmark.
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
