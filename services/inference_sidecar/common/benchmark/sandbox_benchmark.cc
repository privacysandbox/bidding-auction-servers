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

// This benchmark measures the performance of the sandbox executor (e.g.,
// bidding server) and the sandbox worker (e.g., inference sidecar).
//
// Run the benchmark as follows:
// builders/tools/bazel-debian run //benchmark:sandbox_benchmark -- \
//   --benchmark_counters_tabular=true --benchmark_repetitions=5 \
//   --benchmark_min_warmup_time=1 > /tmp/report.txt

// How to read the benchmark results:
//
// Benchmark name:
// * `BM_Multiworker_*`: Triggers multiple sandbox workers. The default
//   benchmarks always use a single sandbox worker.
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
// * `NumWorkers`: The number of sandbox workers (or, inference sidecars)
//   running.

#include <memory>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "benchmark/benchmark.h"
#include "benchmark/request_utils.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_executor.h"
#include "utils/file_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kGrpcInferenceSidecarBinary = "inference_sidecar";
constexpr absl::string_view kIpcInferenceSidecarBinary =
    "benchmark/ipc_inference_sidecar";
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
constexpr char kNumWorkers[] = "NumWorkers";
constexpr int kMaxThreads = 32;

static void ExportMetrics(benchmark::State& state) {
  state.SetItemsProcessed(state.iterations());

  state.counters["Throughput"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate);
  state.counters["Latency"] =
      benchmark::Counter(state.iterations(), benchmark::Counter::kIsRate |
                                                 benchmark::Counter::kInvert);
}

static void BM_Multiworker_Predict_GRPC(benchmark::State& state) {
  SandboxExecutor executor(kGrpcInferenceSidecarBinary, {"{}"});
  CHECK_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  std::shared_ptr<grpc::Channel> client_channel =
      grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                        executor.FileDescriptor());
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(client_channel);

  RegisterModelRequest register_model_request;
  RegisterModelResponse register_model_response;
  CHECK(PopulateRegisterModelRequest(kTestModelPath, register_model_request)
            .ok());
  grpc::ClientContext context;
  grpc::Status status = stub->RegisterModel(&context, register_model_request,
                                            &register_model_response);
  CHECK(status.ok()) << status.error_message();

  for (auto _ : state) {
    state.PauseTiming();
    std::string input = StringFormat(kJsonString);
    state.ResumeTiming();

    PredictRequest predict_request;
    predict_request.set_input(input);

    PredictResponse predict_response;
    grpc::ClientContext context;
    grpc::Status status =
        stub->Predict(&context, predict_request, &predict_response);
    CHECK(status.ok()) << status.error_message();
  }

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  CHECK(result.ok());
  CHECK_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  CHECK_EQ(result->reason_code(), 0);

  state.counters[kNumWorkers] = 1;
  ExportMetrics(state);
}

static void BM_Predict_GRPC(benchmark::State& state) {
  static std::unique_ptr<SandboxExecutor> executor = nullptr;
  static std::unique_ptr<InferenceService::StubInterface> stub = nullptr;

  if (state.thread_index() == 0) {
    const std::vector<std::string> arg = {"{}"};
    executor =
        std::make_unique<SandboxExecutor>(kGrpcInferenceSidecarBinary, arg);
    CHECK_EQ(executor->StartSandboxee().code(), absl::StatusCode::kOk);

    std::shared_ptr<grpc::Channel> client_channel =
        grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                          executor->FileDescriptor());
    stub = InferenceService::NewStub(client_channel);

    RegisterModelRequest register_model_request;
    RegisterModelResponse register_model_response;
    CHECK(PopulateRegisterModelRequest(kTestModelPath, register_model_request)
              .ok());
    grpc::ClientContext context;
    grpc::Status status = stub->RegisterModel(&context, register_model_request,
                                              &register_model_response);
    CHECK(status.ok()) << status.error_message();
  }

  for (auto _ : state) {
    state.PauseTiming();
    std::string input = StringFormat(kJsonString);
    state.ResumeTiming();

    PredictRequest predict_request;
    predict_request.set_input(input);

    PredictResponse predict_response;
    grpc::ClientContext context;
    grpc::Status status =
        stub->Predict(&context, predict_request, &predict_response);
    CHECK(status.ok()) << status.error_message();
  }

  if (state.thread_index() == 0) {
    absl::StatusOr<sandbox2::Result> result = executor->StopSandboxee();
    CHECK(result.ok());
    CHECK_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
    CHECK_EQ(result->reason_code(), 0);

    state.counters[kNumWorkers] = 1;
  }

  ExportMetrics(state);
}

static void BM_Multiworker_Predict_IPC(benchmark::State& state) {
  SandboxExecutor executor(kIpcInferenceSidecarBinary, {"{}"});
  CHECK_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);
  sandbox2::Comms comms(executor.FileDescriptor());

  RegisterModelRequest register_request;
  CHECK(PopulateRegisterModelRequest(kTestModelPath, register_request).ok());
  CHECK(comms.SendProtoBuf(register_request));

  for (auto _ : state) {
    state.PauseTiming();
    std::string input = StringFormat(kJsonString);
    state.ResumeTiming();

    PredictRequest predict_request;
    predict_request.set_input(input);
    CHECK(comms.SendProtoBuf(predict_request));

    PredictResponse predict_response;
    const int kNumTrials = 10;
    bool success = false;
    for (int i = 0; i < kNumTrials; i++) {
      success = comms.RecvProtoBuf(&predict_response);
      if (success) break;
      absl::SleepFor(absl::Microseconds(500));
    }
    CHECK(success);
  }

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  CHECK(result.ok());
  CHECK_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  CHECK_EQ(result->reason_code(), 0);

  state.counters[kNumWorkers] = 1;
  ExportMetrics(state);
}

static void BM_Predict_IPC(benchmark::State& state) {
  static absl::Mutex mutex(absl::kConstInit);
  static std::unique_ptr<SandboxExecutor> executor = nullptr;
  static std::unique_ptr<sandbox2::Comms> comms = nullptr;

  if (state.thread_index() == 0) {
    const std::vector<std::string> arg = {"{}"};
    executor =
        std::make_unique<SandboxExecutor>(kIpcInferenceSidecarBinary, arg);
    CHECK_EQ(executor->StartSandboxee().code(), absl::StatusCode::kOk);
    comms = std::make_unique<sandbox2::Comms>(executor->FileDescriptor());

    RegisterModelRequest register_request;
    CHECK(PopulateRegisterModelRequest(kTestModelPath, register_request).ok());
    CHECK(comms->SendProtoBuf(register_request));
  }

  for (auto _ : state) {
    state.PauseTiming();
    std::string input = StringFormat(kJsonString);
    state.ResumeTiming();

    PredictRequest predict_request;
    predict_request.set_input(input);

    absl::MutexLock lock(&mutex);

    CHECK(comms->SendProtoBuf(predict_request));

    PredictResponse predict_response;
    const int kNumTrials = 10;
    bool success = false;
    for (int i = 0; i < kNumTrials; i++) {
      success = comms->RecvProtoBuf(&predict_response);
      if (success) break;
      absl::SleepFor(absl::Microseconds(500));
    }
    CHECK(success);
  }

  if (state.thread_index() == 0) {
    absl::StatusOr<sandbox2::Result> result = executor->StopSandboxee();
    CHECK(result.ok());
    CHECK_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
    CHECK_EQ(result->reason_code(), 0);

    state.counters[kNumWorkers] = 1;
  }

  ExportMetrics(state);
}

// BM_Register_IPC is not implemented. It's too slow to run the microbenchmark
// because it requires a new sandbox worker per model registration.
static void BM_Register_GRPC(benchmark::State& state) {
  SandboxExecutor executor(kGrpcInferenceSidecarBinary, {"{}"});
  CHECK_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  std::shared_ptr<grpc::Channel> client_channel =
      grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                        executor.FileDescriptor());
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(client_channel);

  RegisterModelRequest register_model_request;
  CHECK(PopulateRegisterModelRequest(kTestModelPath, register_model_request)
            .ok());

  int iter = 0;
  for (auto _ : state) {
    state.PauseTiming();
    std::string new_model_path =
        absl::StrCat(register_model_request.model_spec().model_path(),
                     state.thread_index(), iter++);
    RegisterModelRequest new_register_model_request =
        CreateRegisterModelRequest(register_model_request, new_model_path);
    state.ResumeTiming();

    grpc::ClientContext context;
    RegisterModelResponse register_model_response;
    grpc::Status status = stub->RegisterModel(
        &context, new_register_model_request, &register_model_response);
    CHECK(status.ok()) << status.error_message();
  }

  ExportMetrics(state);

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  CHECK(result.ok());
  CHECK_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  CHECK_EQ(result->reason_code(), 0);

  state.counters[kNumWorkers] = 1;
  ExportMetrics(state);
}

// Register the function as a benchmark
BENCHMARK(BM_Register_GRPC)->MeasureProcessCPUTime()->UseRealTime();

// Use a single sandbox worker (or, inference sidecar) to run the execution in
// parallel.
BENCHMARK(BM_Predict_GRPC)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

// BM_Multiworker benchmarks trigger one sandbox worker (or, inference
// sidecar) per Thread. NumWorkers counter is the number of the running
// sandbox workers in the benchmark.
BENCHMARK(BM_Multiworker_Predict_GRPC)
    ->ThreadRange(1, kMaxThreads)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

// Run the benchmark
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
