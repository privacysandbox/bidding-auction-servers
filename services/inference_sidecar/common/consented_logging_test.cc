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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_executor.h"
#include "utils/file_util.h"

#include "test_constants.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

constexpr absl::string_view kInferenceSidecarBinary = "inference_sidecar";
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

class ConsentedLoggingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    flag_saver_ = std::make_unique<absl::FlagSaver>();
    absl::SetFlag(&FLAGS_testonly_allow_policies_for_bazel, true);

    executor_ = std::make_unique<SandboxExecutor>(
        kInferenceSidecarBinary,
        std::vector<std::string>{std::string(kRuntimeConfig)});
    ASSERT_EQ(executor_->StartSandboxee().code(), absl::StatusCode::kOk);

    client_channel_ = grpc::CreateInsecureChannelFromFd(
        "GrpcChannel", executor_->FileDescriptor());
    stub_ = InferenceService::NewStub(client_channel_);

    RegisterModelRequest register_model_request;
    ASSERT_TRUE(
        PopulateRegisterModelRequest(kTestModelPath, register_model_request)
            .ok());

    grpc::ClientContext context;
    RegisterModelResponse register_model_response;
    grpc::Status status = stub_->RegisterModel(&context, register_model_request,
                                               &register_model_response);
    ASSERT_TRUE(status.ok()) << status.error_message();
  }

  void TearDown() override {
    absl::StatusOr<sandbox2::Result> result = executor_->StopSandboxee();
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
    ASSERT_EQ(result->reason_code(), 0);
  }

  std::unique_ptr<absl::FlagSaver> flag_saver_;
  std::unique_ptr<SandboxExecutor> executor_;
  std::shared_ptr<grpc::Channel> client_channel_;
  std::unique_ptr<InferenceService::StubInterface> stub_;
};

TEST_F(ConsentedLoggingTest, LogIfConsented) {
  PredictRequest predict_request;
  predict_request.set_input(kJsonString);
  predict_request.set_is_consented(true);
  PredictResponse predict_response;

  grpc::ClientContext context;
  grpc::Status status =
      stub_->Predict(&context, predict_request, &predict_response);
  ASSERT_TRUE(status.ok()) << status.error_message();

  ASSERT_THAT(predict_response.debug_info().logs(), Not(IsEmpty()));
  EXPECT_THAT(predict_response.debug_info().logs(0),
              HasSubstr(kConsentedLogMsg));
}

TEST_F(ConsentedLoggingTest, NoConsentSkipLogging) {
  PredictRequest predict_request;
  predict_request.set_input(kJsonString);
  predict_request.set_is_consented(false);
  PredictResponse predict_response;

  grpc::ClientContext context;
  grpc::Status status =
      stub_->Predict(&context, predict_request, &predict_response);
  ASSERT_TRUE(status.ok()) << status.error_message();

  EXPECT_THAT(predict_response.debug_info().logs(), IsEmpty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
