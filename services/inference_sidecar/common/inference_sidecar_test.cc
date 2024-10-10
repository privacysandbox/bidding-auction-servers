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

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "gtest/gtest.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_executor.h"
#include "utils/file_util.h"

#include "test_constants.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kInferenceSidecarBinary = "inference_sidecar";
constexpr absl::string_view kTestModelPath = "test_model";
constexpr char kJsonString[] = R"json({
  "request" : [{
    "model_path" : "test_model",
    "tensors" : [
    {
      "tensor_name": "double1",
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

TEST(InferenceSidecarTest, RegisterModelAndRunInference_Grpc) {
  absl::FlagSaver flag_saver;
  absl::SetFlag(&FLAGS_testonly_allow_policies_for_bazel, true);

  SandboxExecutor executor(kInferenceSidecarBinary,
                           {std::string(kRuntimeConfig)});
  ASSERT_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  std::shared_ptr<grpc::Channel> client_channel =
      grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                        executor.FileDescriptor());
  std::unique_ptr<InferenceService::StubInterface> stub =
      InferenceService::NewStub(client_channel);

  RegisterModelRequest register_model_request;
  RegisterModelResponse register_model_response;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelPath, register_model_request)
          .ok());
  {
    grpc::ClientContext context;
    grpc::Status status = stub->RegisterModel(&context, register_model_request,
                                              &register_model_response);
    EXPECT_TRUE(status.ok()) << status.error_message();
  }

  const int kNumThreads = 100;
  const int kNumIterations = 5;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&stub]() {
      PredictRequest predict_request;
      predict_request.set_input(kJsonString);
      PredictResponse predict_response;
      for (int j = 0; j < kNumIterations; j++) {
        grpc::ClientContext context;
        grpc::Status status =
            stub->Predict(&context, predict_request, &predict_response);
        EXPECT_TRUE(status.ok()) << status.error_message();
      }
    }));
  }

  // Waits for all threads to finish.
  for (auto& thread : threads) {
    thread.join();
  }

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  ASSERT_EQ(result->reason_code(), 0);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
