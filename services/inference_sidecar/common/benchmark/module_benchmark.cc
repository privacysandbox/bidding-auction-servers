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

// This benchmark measures the performance of the inference module.
//
// Run the benchmark as follows:
// builders/tools/bazel-debian run //benchmark:module_benchmark -- \
//   --benchmark_counters_tabular=true --benchmark_repetitions=5 \
//   --benchmark_min_warmup_time=1 > /tmp/report.txt

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

#include <memory>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/notification.h"
#include "benchmark/benchmark.h"
#include "benchmark/request_utils.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/file_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kTestModelPath = "test_model";
constexpr char kJsonString[] = R"json({
  "request" : [{
    "model_path" : "test_model",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1,
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
}]
    })json";
constexpr int kMaxThreads = 64;
constexpr int kRegisterMaxThreads = 4;

static void ExportMetrics(benchmark::State& state) {
  state.SetItemsProcessed(state.iterations());

  state.counters["Throughput"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.counters["Latency"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate |
                                                 benchmark::Counter::kInvert);
}

class ModuleFixture : public benchmark::Fixture {
 public:
  void SetUp(::benchmark::State& state) {
    if (state.thread_index() != 0) {
      // Blocks until the model is loaded on the main thread.
      model_loaded_.WaitForNotification();
      return;
    }
    completion_barrier_ = absl::make_unique<absl::Barrier>(state.threads());

    InferenceSidecarRuntimeConfig config;
    module_ = ModuleInterface::Create(config);

    CHECK(PopulateRegisterModelRequest(kTestModelPath, register_model_request_)
              .ok());
    absl::StatusOr response = module_->RegisterModel(register_model_request_);
    CHECK(response.ok()) << response.status().message();
    model_loaded_.Notify();
  }
  void TearDown(::benchmark::State& state) {
    // The last thread reaching the barrier will perform the cleanup.
    if (!completion_barrier_->Block()) return;
    module_.reset();
  }

 protected:
  std::unique_ptr<ModuleInterface> module_;
  RegisterModelRequest register_model_request_;
  absl::Notification model_loaded_;
  std::unique_ptr<absl::Barrier> completion_barrier_;
};

BENCHMARK_DEFINE_F(ModuleFixture, BM_Predict)(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    std::string input = StringFormat(kJsonString);
    state.ResumeTiming();

    PredictRequest predict_request;
    predict_request.set_input(input);

    absl::StatusOr response = module_->Predict(predict_request);
    CHECK(response.ok()) << response.status().message();
  }

  ExportMetrics(state);
}

BENCHMARK_DEFINE_F(ModuleFixture, BM_Register)(benchmark::State& state) {
  int iter = 0;
  for (auto _ : state) {
    state.PauseTiming();
    std::string new_model_path =
        absl::StrCat(register_model_request_.model_spec().model_path(),
                     state.thread_index(), iter++);
    RegisterModelRequest new_register_model_request =
        CreateRegisterModelRequest(register_model_request_, new_model_path);
    state.ResumeTiming();

    absl::StatusOr response =
        module_->RegisterModel(new_register_model_request);
    CHECK(response.ok()) << response.status().message();
  }

  ExportMetrics(state);
}

// Registers the functions to the benchmark.
BENCHMARK_REGISTER_F(ModuleFixture, BM_Register)
    ->ThreadRange(1, kRegisterMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
BENCHMARK_REGISTER_F(ModuleFixture, BM_Predict)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

// Runs the benchmark.
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
