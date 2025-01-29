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

#include "services/bidding_service/inference/inference_utils.h"

#include <gmock/gmock-matchers.h>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "services/bidding_service/inference/inference_flags.h"
#include "src/roma/interface/roma.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kSidecarBinary =
    "__main__/external/inference_common/inference_sidecar";
constexpr absl::string_view kInit = "non-empty";
constexpr absl::string_view kTestModelPath =
    "external/inference_common/testdata/models/tensorflow_1_mib_saved_model.pb";
constexpr absl::string_view kBucketName = "test_bucket";
constexpr absl::string_view kRuntimeConfig = R"json({
  "num_interop_threads": 4,
  "num_intraop_threads": 5,
  "module_name": "test",
  "cpuset": [0, 1]
})json";

class InferenceUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::SetFlag(&FLAGS_testonly_allow_policies_for_bazel, true);
    absl::SetFlag(&FLAGS_inference_sidecar_binary_path,
                  GetFilePath(kSidecarBinary));
    absl::SetFlag(&FLAGS_inference_sidecar_runtime_config, kRuntimeConfig);
  }

 private:
  absl::FlagSaver flag_saver_;
};

// TODO(b/322030670): Making static SandboxExecutor compatible with multiple
// tests.

TEST_F(InferenceUtilsTest, TestAPIOutputs) {
  SandboxExecutor& inference_executor = Executor();
  CHECK_EQ(inference_executor.StartSandboxee().code(), absl::StatusCode::kOk);

  // register a model
  ASSERT_TRUE(RegisterModelsFromLocal({std::string(kTestModelPath)}).ok());
  google::scp::roma::proto::FunctionBindingIoProto input_output_proto;
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper{
      input_output_proto, {}};
  wrapper.io_proto.set_input_string(absl::StrCat("1.0"));
  wrapper.io_proto.set_output_string(kInit);
  RunInference(wrapper);
  // TODO(b/317124477): Update the output string after Tensorflow execution
  // logic. Currently, this test uses a test inference module that doesn't
  // populate the output string.
  ASSERT_EQ(wrapper.io_proto.output_string(), "0.57721");

  // make sure GetModelPaths returns the registered model
  google::scp::roma::proto::FunctionBindingIoProto input_output_proto_1;
  google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext> wrapper_1{
      input_output_proto_1, {}};
  GetModelPaths(wrapper_1);
  ASSERT_EQ(wrapper_1.io_proto.output_string(),
            "[\"" + std::string(kTestModelPath) + "\"]");

  absl::StatusOr<sandbox2::Result> result = inference_executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  ASSERT_EQ(result->reason_code(), 0);

  // Propagates JS error to client even when inference sidecar is not reachable.
  RunInference(wrapper);
  EXPECT_THAT(
      wrapper.io_proto.output_string(),
      ::testing::StartsWith(
          R"({"response":[{"error":{"error_type":"GRPC","description")"));
}

TEST_F(InferenceUtilsTest, RegisterModelsFromLocal_NoPath_Error) {
  EXPECT_EQ(RegisterModelsFromLocal({}).code(), absl::StatusCode::kNotFound);
}

TEST_F(InferenceUtilsTest, RegisterModelsFromLocal_EmptyPath_Error) {
  EXPECT_EQ(RegisterModelsFromLocal({""}).code(), absl::StatusCode::kNotFound);
}

TEST_F(InferenceUtilsTest, RegisterModelsFromBucket_Error) {
  EXPECT_EQ(
      RegisterModelsFromBucket("", {std::string(kTestModelPath)}, {}).code(),
      absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      RegisterModelsFromBucket(kBucketName, {std::string(kTestModelPath)}, {})
          .code(),
      absl::StatusCode::kNotFound);
  EXPECT_EQ(RegisterModelsFromBucket(kBucketName, {}, {}).code(),
            absl::StatusCode::kNotFound);
  EXPECT_EQ(RegisterModelsFromBucket(kBucketName, {""}, {{"", ""}}).code(),
            absl::StatusCode::kNotFound);
}

TEST_F(InferenceUtilsTest, GetModelResponseToJsonOuput) {
  GetModelPathsResponse get_model_paths_response;
  EXPECT_EQ("[]", GetModelResponseToJson(get_model_paths_response));
  ModelSpec* spec;

  spec = get_model_paths_response.add_model_specs();
  spec->set_model_path("a");

  EXPECT_EQ("[\"a\"]", GetModelResponseToJson(get_model_paths_response));

  spec = get_model_paths_response.add_model_specs();
  spec->set_model_path("b");

  EXPECT_EQ("[\"a\",\"b\"]", GetModelResponseToJson(get_model_paths_response));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
