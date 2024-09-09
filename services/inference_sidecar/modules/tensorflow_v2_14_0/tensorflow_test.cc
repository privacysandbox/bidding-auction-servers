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
#include <gmock/gmock-matchers.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/file_util.h"
#include "utils/inference_metric_util.h"
#include "utils/test_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using ::testing::HasSubstr;
using ::testing::StartsWith;
constexpr absl::string_view kModel1Dir = "./benchmark_models/pcvr";
constexpr absl::string_view kModel2Dir = "./benchmark_models/pctr";
constexpr absl::string_view kEmbeddingModelDir = "./benchmark_models/embedding";
constexpr absl::string_view kStatefulModelDir = "./benchmark_models/stateful";

TEST(TensorflowModuleTest, Failure_RegisterModelWithEmptyPath) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  register_request.mutable_model_spec()->set_model_path("");
  absl::StatusOr<RegisterModelResponse> status_or =
      tensorflow_module->RegisterModel(register_request);
  ASSERT_FALSE(status_or.ok());

  EXPECT_EQ(status_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status_or.status().message(), "Model path is empty");
}

TEST(TensorflowModuleTest, Failure_RegisterModelAlreadyRegistered) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);

  // First model registration
  RegisterModelRequest register_request_1;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kModel1Dir, register_request_1).ok());
  absl::StatusOr<RegisterModelResponse> response_1 =
      tensorflow_module->RegisterModel(register_request_1);
  ASSERT_TRUE(response_1.ok());

  // Second registration attempt with the same model path
  RegisterModelRequest register_request_2;
  // Same model path as the first attempt
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kModel1Dir, register_request_2).ok());
  absl::StatusOr<RegisterModelResponse> response_2 =
      tensorflow_module->RegisterModel(register_request_2);

  ASSERT_FALSE(response_2.ok());
  EXPECT_EQ(response_2.status().code(), absl::StatusCode::kAlreadyExists);
  EXPECT_EQ(response_2.status().message(), "Model " + std::string(kModel1Dir) +
                                               " has already been registered");
}

TEST(TensorflowModuleTest, Failure_RegisterModelLoadError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);

  // Setting the model path to a non-existent directory
  RegisterModelRequest register_request;
  register_request.mutable_model_spec()->set_model_path("./pcvr");
  absl::StatusOr<RegisterModelResponse> status_or =
      tensorflow_module->RegisterModel(register_request);

  // Verifying that the registration fails with an InternalError due to loading
  // failure
  ASSERT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().code(), absl::StatusCode::kInternal);
  EXPECT_NE(std::string::npos,
            status_or.status().message().find("Error loading model: ./pcvr"));
}

TEST(TensorflowModuleTest, Success_RegisterModel) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  absl::StatusOr<RegisterModelResponse> status_or =
      tensorflow_module->RegisterModel(register_request);
  ASSERT_TRUE(status_or.ok());
}

TEST(TensorflowModuleTest, JsonError_PredictInvalidJson) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  PredictRequest predict_request;
  predict_request.set_input("");

  absl::StatusOr<PredictResponse> predict_response =
      tensorflow_module->Predict(predict_request);

  ASSERT_TRUE(predict_response.ok());
  EXPECT_THAT(
      predict_response->output(),
      StartsWith(
          R"({"response":[{"error":{"error_type":"INPUT_PARSING","description")"));
}

constexpr char kPcvrJsonRequest[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    }
  ]
}]
    })json";

constexpr absl::string_view kPcvrResponse =
    "{\"response\":[{\"model_path\":\"./benchmark_models/"
    "pcvr\",\"tensors\":[{\"tensor_name\":\"StatefulPartitionedCall:"
    "0\",\"tensor_shape\":[1,1],\"data_type\":\"FLOAT\",\"tensor_"
    "content\":[0.019116628915071489]}]}]}";

TEST(TensorflowModuleTest, JsonError_PredictModelNotRegistered) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequest);

  absl::StatusOr<PredictResponse> predict_response =
      tensorflow_module->Predict(predict_request);

  ASSERT_TRUE(predict_response.ok());
  EXPECT_THAT(
      predict_response->output(),
      StartsWith(
          R"({"response":[{"model_path":"./benchmark_models/pcvr","error":{"error_type":"MODEL_NOT_FOUND","description")"));
}

TEST(TensorflowModuleTest, Success_Predict) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequest);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  ASSERT_EQ(response.output(), kPcvrResponse);
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
}

TEST(TensorflowModuleTest, Success_Predict_ValidateMetrics) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequest);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  ASSERT_EQ(response.output(), kPcvrResponse);
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
  CheckMetric(response.metrics(), "kInferenceRequestCount", 1);
  CheckMetric(response.metrics(), "kInferenceRequestSize", 1435);
  CheckMetric(response.metrics(), "kInferenceResponseSize", 415);

  auto it = response.metrics().find("kInferenceRequestDuration");
  ASSERT_NE(it, response.metrics().end())
      << "kInferenceRequestDuration metric is missing.";
  EXPECT_GT(it->second.value(), 0)
      << "kInferenceRequestDuration should be greater than zero.";
}

TEST(TensorflowModuleTest, Success_PredictWithConsentedRequest) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequest);
  predict_request.set_is_consented(true);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  ASSERT_EQ(response.output(), kPcvrResponse);
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
}

constexpr char kPcvrJsonRequestBatchSize2[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, Success_PredictBatchSize2) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestBatchSize2);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  ASSERT_EQ(response.output(),
            "{\"response\":[{\"model_path\":\"./benchmark_models/"
            "pcvr\",\"tensors\":[{\"tensor_name\":\"StatefulPartitionedCall:"
            "0\",\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_"
            "content\":[0.019116630777716638,0.1847093403339386]}]}]}");
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
}

TEST(TensorflowModuleTest, Success_RegisterModelWithWarmUpData) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  register_request.set_warm_up_batch_request_json(kPcvrJsonRequestBatchSize2);
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());
}

constexpr char kPcvrJsonRequestBatchSizeWithWrongPath[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/non_exist_path",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
}]
    })json";

constexpr char kPcvrJsonRequestBatchSizeWithWrongTensor[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, Failure_RegisterModelWithInvalidWarmUpData) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request_wrong_path;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kModel1Dir, register_request_wrong_path)
          .ok());
  register_request_wrong_path.set_warm_up_batch_request_json(
      kPcvrJsonRequestBatchSizeWithWrongPath);
  ASSERT_FALSE(
      tensorflow_module->RegisterModel(register_request_wrong_path).ok());

  RegisterModelRequest register_request_wrong_tensor;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kModel1Dir, register_request_wrong_tensor)
          .ok());
  register_request_wrong_tensor.set_warm_up_batch_request_json(
      kPcvrJsonRequestBatchSizeWithWrongTensor);
  ASSERT_FALSE(
      tensorflow_module->RegisterModel(register_request_wrong_tensor).ok());
}

constexpr char kPcvrJsonRequestMissingTensorName[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, JsonError_PredictMissingTensorName) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestMissingTensorName);
  absl::StatusOr<PredictResponse> predict_response =
      tensorflow_module->Predict(predict_request);

  ASSERT_TRUE(predict_response.ok());
  EXPECT_THAT(
      predict_response->output(),
      StartsWith(
          R"({"response":[{"model_path":"./benchmark_models/pcvr","error":{"error_type":"INPUT_PARSING","description")"));
}

constexpr char kPcvrJsonRequestInvalidTensor[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["seven"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, JsonError_PredictInvalidTensor) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestInvalidTensor);
  absl::StatusOr<PredictResponse> predict_response =
      tensorflow_module->Predict(predict_request);

  ASSERT_TRUE(predict_response.ok());
  EXPECT_THAT(
      predict_response->output(),
      StartsWith(
          R"({"response":[{"model_path":"./benchmark_models/pcvr","error":{"error_type":"INPUT_PARSING","description")"));
}

constexpr char kPcvrJsonRequestWrongInputTensor[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 2
      ],
      "tensor_content": ["1.1", "1.1", "1.1", "1.1"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, JsonError_PredictModelExecutionError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kModel1Dir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestWrongInputTensor);
  absl::StatusOr<PredictResponse> predict_response =
      tensorflow_module->Predict(predict_request);

  ASSERT_TRUE(predict_response.ok());
  EXPECT_THAT(
      predict_response->output(),
      StartsWith(
          R"({"response":[{"model_path":"./benchmark_models/pcvr","error":{"error_type":"MODEL_EXECUTION","description")"));
}

constexpr char kPcvrJsonRequestWith2Model[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
},
{
    "model_path" : "./benchmark_models/pctr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["8", "4"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["8", "4"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["8", "4"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["8", "4"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["8", "4"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, Success_PredictWith2Models) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request_1;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kModel1Dir, register_request_1).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request_1).ok());

  RegisterModelRequest register_request_2;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kModel2Dir, register_request_2).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request_2).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestWith2Model);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  EXPECT_EQ(response.output(),
            "{\"response\":[{\"model_path\":\"./benchmark_models/"
            "pcvr\",\"tensors\":[{\"tensor_name\":\"StatefulPartitionedCall:"
            "0\",\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_"
            "content\":[0.019116630777716638,0.1847093403339386]}]},{\"model_"
            "path\":\"./benchmark_models/"
            "pctr\",\"tensors\":[{\"tensor_name\":\"StatefulPartitionedCall:"
            "0\",\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_"
            "content\":[0.14649735391139985,0.2522672712802887]}]}]}");
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
}

constexpr char kPcvrJsonRequestWith1ModelVariedSize[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
},
{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest,
     PredictSameModelVariedBatchSizesMultipleRequestsSuccess) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(std::string(kModel1Dir), register_request)
          .ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestWith1ModelVariedSize);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  EXPECT_EQ(response.output(),
            "{\"response\":[{\"model_path\":\"./benchmark_models/"
            "pcvr\",\"tensors\":[{\"tensor_name\":\"StatefulPartitionedCall:"
            "0\",\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_"
            "content\":[0.019116630777716638,0.1847093403339386]}]},{\"model_"
            "path\":\"./benchmark_models/"
            "pcvr\",\"tensors\":[{\"tensor_name\":\"StatefulPartitionedCall:"
            "0\",\"tensor_shape\":[1,1],\"data_type\":\"FLOAT\",\"tensor_"
            "content\":[0.010360434651374817]}]}]}");
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
}

constexpr char kPcvrJsonRequestEmbeddingModel[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/embedding",
    "tensors" : [
    {
      "tensor_name": "serving_default_ad_embed:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_pub_embed:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.32", "0.12", "0.98", "0.11"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["7"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, Success_PredictEmbed) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kEmbeddingModelDir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kPcvrJsonRequestEmbeddingModel);
  absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_status.ok());
  PredictResponse response = predict_status.value();
  ASSERT_FALSE(response.output().empty());
  // The order of output tensors in Tensorflow is not constant so we check ech
  // tensor output separately.
  EXPECT_TRUE(absl::StrContains(
      response.output(),
      "{\"tensor_name\":\"StatefulPartitionedCall:0\",\"tensor_shape\":[1,6],"
      "\"data_type\":\"FLOAT\",\"tensor_content\":[0.7276111245155335,0."
      "0728105902671814,0.11053494364023209,0.6876803636550903,0."
      "3626940846443176,0.13941356539726258]}"));
  EXPECT_TRUE(absl::StrContains(
      response.output(),
      "{\"tensor_name\":\"StatefulPartitionedCall:1\",\"tensor_shape\":[1,1],"
      "\"data_type\":\"INT32\",\"tensor_content\":[0]}"));
  ASSERT_FALSE(response.metrics().empty());
  EXPECT_EQ(response.metrics().size(), 6);
}

constexpr char kMixedValidInvalidBatchJsonRequest[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
},
{
    "model_path" : "./non-existent",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["7", "3"]
    }
  ]
},
{
    "model_path" : "./benchmark_models/pcvr",
    "tensors" : [
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.33", "0.13", "0.97", "0.12"]
    },
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["8"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, CanReturnPartialBatchOutputWithError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request_1;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(std::string(kModel1Dir), register_request_1)
          .ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request_1).ok());
  RegisterModelRequest register_request_2;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(std::string(kModel2Dir), register_request_2)
          .ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request_2).ok());

  PredictRequest predict_request;
  predict_request.set_input(kMixedValidInvalidBatchJsonRequest);
  absl::StatusOr predict_output = tensorflow_module->Predict(predict_request);
  ASSERT_TRUE(predict_output.ok());
  EXPECT_THAT(predict_output->output(),
              AllOf(HasSubstr("MODEL_EXECUTION"), HasSubstr("MODEL_NOT_FOUND"),
                    HasSubstr("\"tensors\":")));
}

constexpr char kJsonRequestStatefulModel[] = R"json({
  "request" : [{
    "model_path" : "./benchmark_models/stateful",
    "tensors" : [
    {
      "tensor_name": "serving_default_input_1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        1, 1
      ],
      "tensor_content": ["0.0"]
    }
  ]
}]
    })json";

TEST(TensorflowModuleTest, Success_NoReset_StatefulModel) {
  const int kIterations = 100;
  InferenceSidecarRuntimeConfig config;
  config.set_model_reset_probability(0.0);

  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kStatefulModelDir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  for (int count = 2; count < kIterations; count++) {
    PredictRequest predict_request;
    predict_request.set_input(kJsonRequestStatefulModel);
    absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
    ASSERT_TRUE(predict_status.ok());
    PredictResponse response = predict_status.value();
    ASSERT_FALSE(response.output().empty());

    EXPECT_TRUE(absl::StrContains(
        response.output(),
        absl::StrCat("{\"tensor_name\":\"StatefulPartitionedCall:0\",\"tensor_"
                     "shape\":[],"
                     "\"data_type\":\"INT32\",\"tensor_content\":[",
                     count, "]}")))
        << response.output();
  }
}

TEST(TensorflowModuleTest, Success_Reset_StatefulModel) {
  const int kIterations = 10;
  InferenceSidecarRuntimeConfig config;
  config.set_model_reset_probability(1.0);

  std::unique_ptr<ModuleInterface> tensorflow_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kStatefulModelDir, register_request).ok());
  ASSERT_TRUE(tensorflow_module->RegisterModel(register_request).ok());

  for (int i = 0; i < kIterations; i++) {
    PredictRequest predict_request;
    predict_request.set_input(kJsonRequestStatefulModel);
    absl::StatusOr predict_status = tensorflow_module->Predict(predict_request);
    ASSERT_TRUE(predict_status.ok());
    PredictResponse response = predict_status.value();
    ASSERT_FALSE(response.output().empty());

    EXPECT_TRUE(absl::StrContains(
        response.output(),
        "{\"tensor_name\":\"StatefulPartitionedCall:0\",\"tensor_shape\":[],"
        "\"data_type\":\"INT32\",\"tensor_content\":[2]}"))
        << response.output();
    absl::SleepFor(absl::Seconds(1));
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
