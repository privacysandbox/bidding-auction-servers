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

#include <torch/torch.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/file_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Simple model returns its input tensor as the output.
constexpr absl::string_view kSimpleModel = "simple_model";
constexpr absl::string_view kTestModelVariedInputs1 = "e2e_model1";
constexpr absl::string_view kTestModelVariedInputs2 = "e2e_model2";
constexpr absl::string_view kTestModelMixedInputsMixedOutputs =
    "mixed_inputs_mixed_outputs_model";
constexpr absl::string_view kStatefulModelDir = "stateful_model";
constexpr int kNumThreads = 100;

TEST(PyTorchModuleRuntimeConfigTest,
     RuntimeConfigInitializationSucceeds_Empty) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);

  // PyTorch runtime threading configuration is set at the process level.
  std::thread t([]() {
    EXPECT_NE(at::get_num_threads(), 0);
    EXPECT_NE(at::get_num_interop_threads(), 0);
  });
  t.join();
}

TEST(PyTorchModuleRuntimeConfigTest, RuntimeConfigInitializationSucceeds) {
  InferenceSidecarRuntimeConfig config;
  config.set_num_intraop_threads(4);
  config.set_num_interop_threads(5);
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);

  // PyTorch runtime threading configuration is set at the process level.
  std::thread t([]() {
    EXPECT_EQ(at::get_num_threads(), 4);
    EXPECT_EQ(at::get_num_interop_threads(), 5);
  });
  t.join();
}

TEST(PyTorchModuleRegisterModelTest,
     RegisterModelWithEmtpyKeyReturnsInvalidArgument) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_EQ(torch_module->RegisterModel(register_request).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(PyTorchModuleRegisterModelTest,
     RegisterModelWithExistingKeyReturnsAlreadyExists) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());
  EXPECT_EQ(torch_module->RegisterModel(register_request).status().code(),
            absl::StatusCode::kAlreadyExists);
}

TEST(PyTorchModuleRegisterModelTest,
     RegisterModelWithNoModelContentReturnsInvalidArgument) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  register_request.mutable_model_spec()->set_model_path("e2e_model1");
  EXPECT_EQ(torch_module->RegisterModel(register_request).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(PyTorchModuleRegisterModelTest, RegisterModelOk) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());
}

TEST(PyTorchModulePredictTest,
     PredictWithoutValidBatchRequestInputReturnsInvalidArgument) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  const absl::StatusOr<PredictResponse> predict_response =
      torch_module->Predict(predict_request);
  ASSERT_EQ(predict_response.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(
      absl::StrContains(predict_response.status().message(),
                        "Encounters batch inference request parsing error:"));
}

constexpr char kInvalidTensorContentRequest[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["seven"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest,
     PredictWithInvalidTensorContentReturnsInvalidArgument) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kInvalidTensorContentRequest);
  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(
      absl::StrContains(result.status().message(), "tensor parsing error"));
}

constexpr char kSimpleRequest[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
}]
    })json";

constexpr absl::string_view kSimpleRequestResponse =
    "{\"response\":[{\"model_path\":\"simple_model\",\"tensors\":[{\"tensor_"
    "shape\":[1],\"data_type\":\"DOUBLE\",\"tensor_content\":[3.14]}]}]}";

TEST(PyTorchModulePredictTest, PredictWithoutValidModelReturnsNotFound) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_EQ(torch_module->RegisterModel(register_request).status().code(),
            absl::StatusCode::kInvalidArgument);

  PredictRequest predict_request;
  *predict_request.mutable_input() = kSimpleRequest;
  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_TRUE(
      absl::StrContains(result.status().message(), "has not been registered"));
}

TEST(PyTorchModulePredictTest, PredictSimpleSuccess) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSimpleRequest);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->output(), kSimpleRequestResponse);
}

TEST(PyTorchModulePredictTest, PredictConsentedRequestSuccess) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSimpleRequest);
  predict_request.set_is_consented(true);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->output(), kSimpleRequestResponse);
}

TEST(PyTorchModuleResetModelTest, NoModel) {
  InferenceSidecarRuntimeConfig config;
  config.set_model_reset_probability(1.0);
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  torch_module->ResetModels();
}

TEST(PyTorchModuleResetModelTest, ResetModelOk) {
  InferenceSidecarRuntimeConfig config;
  config.set_model_reset_probability(1.0);
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSimpleRequest);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->output(), kSimpleRequestResponse);

  torch_module->ResetModels();
}

constexpr char kNotRegisteredModelRequest[] = R"json({
  "request" : [{
    "model_path" : "not_registered",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictReturnsNotFoundError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelVariedInputs1, register_request)
          .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  *predict_request.mutable_input() = kNotRegisteredModelRequest;
  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
  EXPECT_EQ(result.status().message(),
            "Requested model 'not_registered' has not been registered");
}

constexpr char kMismatchedInput[] = R"json({
  "request" : [{
    "model_path" : "e2e_model1",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictReturnsInternalError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelVariedInputs1, register_request)
          .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  *predict_request.mutable_input() =
      kMismatchedInput;  // kMismatchedInput is not compatible with
                         // kTestModelVariedInputs1 model.
  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  EXPECT_TRUE(absl::StrContains(result.status().message(),
                                "encounters an exception during evaluation"));
}

constexpr char kSimpleRequest2Elements[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 2
      ],
      "tensor_content": ["3.14", "2.718"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictSimpleSuccessShape1x2) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  *predict_request.mutable_input() = kSimpleRequest2Elements;
  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result->output(),
            "{\"response\":[{\"model_path\":\"simple_model\",\"tensors\":[{"
            "\"tensor_shape\":[1,2],\"data_type\":\"DOUBLE\",\"tensor_"
            "content\":[3.14,2.718]}]}]}");
}

constexpr char kSimpleRequestBatchSize2[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["3.14", "2.718"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictSimpleBatchSize2Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSimpleRequestBatchSize2);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->output(),
            "{\"response\":[{\"model_path\":\"simple_model\",\"tensors\":[{"
            "\"tensor_shape\":[2,1],\"data_type\":\"DOUBLE\",\"tensor_"
            "content\":[3.14,2.718]}]}]}");
}

constexpr char kSameModelSameBatchSizeMultipleRequests[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
},
{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["2.718"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest,
     PredictSameModelSameBatchSizeMultipleRequestsSuccess) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSameModelSameBatchSizeMultipleRequests);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(
      result->output(),
      "{\"response\":[{\"model_path\":\"simple_model\",\"tensors\":[{\"tensor_"
      "shape\":[1],\"data_type\":\"DOUBLE\",\"tensor_content\":[3.14]}]},{"
      "\"model_path\":\"simple_model\",\"tensors\":[{\"tensor_shape\":[1],"
      "\"data_type\":\"DOUBLE\",\"tensor_content\":[2.718]}]}]}");
}

constexpr char kSameModelVariedBatchSizesMultipleRequests[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        2, 1
      ],
      "tensor_content": ["3.14", "1.00"]
    }
  ]
},
{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        3, 1
      ],
      "tensor_content": ["2.718", "1.00", "1.00"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest,
     PredictSameModelVariedBatchSizesMultipleRequestsSuccess) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSameModelVariedBatchSizesMultipleRequests);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(
      result->output(),
      "{\"response\":[{\"model_path\":\"simple_model\",\"tensors\":[{\"tensor_"
      "shape\":[2,1],\"data_type\":\"DOUBLE\",\"tensor_content\":[3.14,1.0]}]},"
      "{\"model_path\":\"simple_model\",\"tensors\":[{\"tensor_shape\":[3,1],"
      "\"data_type\":\"DOUBLE\",\"tensor_content\":[2.718,1.0,1.0]}]}]}");
}

constexpr char kRequestsWithMultipleInvalidInputs[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
    "tensor_content": ["seven"]
    }
  ]
},
{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1, 2
      ],
      "tensor_content": ["3.14", "2.718"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictMutilpleInvalidInputsReturnsFirstError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kRequestsWithMultipleInvalidInputs);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(
      absl::StrContains(result.status().message(), "tensor parsing error"));
}

constexpr char kBothValidAndInvalidInputs[] = R"json({
  "request" : [{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3.14"]
    }
  ]
},
{
    "model_path" : "simple_model",
    "tensors" : [
    {
      "data_type": "DOUBLE",
      "tensor_shape": [
        1
      ],
    "tensor_content": ["seven"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictBothValidAndInvalidInputsReturnsError) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kBothValidAndInvalidInputs);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_TRUE(
      absl::StrContains(result.status().message(), "tensor parsing error"));
}

constexpr char kVariedInputsRequestBatchSize1[] = R"json({
  "request" : [{
    "model_path" : "e2e_model1",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["8"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["6"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["18"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["11"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["9"]
    },
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.4313", "0.3381", "0.3103", "0.3150", "0.9595", "0.7862", "0.6386", "0.9695", "0.0469", "0.5807"]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        1, 10
      ],
      "tensor_content": ["0.8201", "0.8321", "0.1021", "0.6779", "0.2152", "0.4805", "0.3957", "0.0825", "0.0230", "0.1711"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictVariedInputsBatchSize1Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelVariedInputs1, register_request)
          .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kVariedInputsRequestBatchSize1);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->output(),
            "{\"response\":[{\"model_path\":\"e2e_model1\",\"tensors\":[{"
            "\"tensor_shape\":[1,1],\"data_type\":\"FLOAT\",\"tensor_content\":"
            "[0.4846605658531189]}]}]}");
}

constexpr char kVariedInputsRequestBatchSize2[] = R"json({
  "request" : [{
    "model_path" : "e2e_model1",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["8", "6"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["18", "11"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["9", "14"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["2", "1"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["10", "4"]
    },
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": [
        "0.7862", "0.6386", "0.9695", "0.0469", "0.5807", "0.8201", "0.8321",
        "0.1021", "0.6779", "0.2152", "0.4805", "0.3957", "0.0825", "0.0230",
        "0.1711", "0.7269", "0.7287", "0.0651", "0.3122", "0.5082"
      ]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": [
        "0.0677", "0.7817", "0.9529", "0.5884", "0.1285", "0.6166", "0.6815",
        "0.8959", "0.2340", "0.0520", "0.7197", "0.5311", "0.3371", "0.2905",
        "0.2422", "0.9047", "0.4137", "0.8606", "0.9463", "0.5633"
      ]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictVariedInputsBatchSize2Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelVariedInputs1, register_request)
          .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kVariedInputsRequestBatchSize2);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result->output(),
            "{\"response\":[{\"model_path\":\"e2e_model1\",\"tensors\":[{"
            "\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_content\":"
            "[0.5412338972091675,0.4757022559642792]}]}]}");
}

constexpr char kMixedInputsBatchSize1[] = R"json({
  "request" : [{
    "model_path" : "mixed_inputs_mixed_outputs_model",
    "tensors" : [
    {
      "data_type": "INT64",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["3"]
    },
    {
      "data_type": "FLOAT",
      "tensor_shape": [
        1, 3
      ],
      "tensor_content": ["0.0394", "1.7093", "-0.2127"]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest, PredictMixedInputsMixedOutputsSuccess) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(PopulateRegisterModelRequest(kTestModelMixedInputsMixedOutputs,
                                           register_request)
                  .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kMixedInputsBatchSize1);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(
      result->output(),
      "{\"response\":[{\"model_path\":\"mixed_inputs_mixed_outputs_model\","
      "\"tensors\":[{\"tensor_shape\":[1,4],\"data_type\":\"FLOAT\",\"tensor_"
      "content\":[-0.3637504279613495,-0.16950955986976624,-0."
      "21080102026462556,-0.2136428952217102]},{\"tensor_shape\":[1,1],"
      "\"data_"
      "type\":\"INT64\",\"tensor_content\":[0]}]}]}");
}

constexpr char kVariedInputsMultipleModelsRequest[] = R"json({
  "request" : [{
    "model_path" : "e2e_model1",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["8", "6"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["18", "11"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["9", "14"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["2", "1"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["10", "4"]
    },
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": [
        "0.7862", "0.6386", "0.9695", "0.0469", "0.5807", "0.8201", "0.8321",
        "0.1021", "0.6779", "0.2152", "0.4805", "0.3957", "0.0825", "0.0230",
        "0.1711", "0.7269", "0.7287", "0.0651", "0.3122", "0.5082"
      ]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": [
        "0.0677", "0.7817", "0.9529", "0.5884", "0.1285", "0.6166", "0.6815",
        "0.8959", "0.2340", "0.0520", "0.7197", "0.5311", "0.3371", "0.2905",
        "0.2422", "0.9047", "0.4137", "0.8606", "0.9463", "0.5633"
      ]
    }
  ]
},
{
    "model_path" : "e2e_model2",
    "tensors" : [
    {
      "tensor_name": "serving_default_int_input1:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["8", "6"]
    },
    {
      "tensor_name": "serving_default_int_input2:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["18", "11"]
    },
    {
      "tensor_name": "serving_default_int_input3:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["9", "14"]
    },
    {
      "tensor_name": "serving_default_int_input4:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["2", "1"]
    },
    {
      "tensor_name": "serving_default_int_input5:0",
      "data_type": "INT64",
      "tensor_shape": [
        2
      ],
      "tensor_content": ["10", "4"]
    },
    {
      "tensor_name": "serving_default_double1:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": [
        "0.7862", "0.6386", "0.9695", "0.0469", "0.5807", "0.8201", "0.8321",
        "0.1021", "0.6779", "0.2152", "0.4805", "0.3957", "0.0825", "0.0230",
        "0.1711", "0.7269", "0.7287", "0.0651", "0.3122", "0.5082"
      ]
    },
    {
      "tensor_name": "serving_default_double2:0",
      "data_type": "FLOAT",
      "tensor_shape": [
        2, 10
      ],
      "tensor_content": [
        "0.0677", "0.7817", "0.9529", "0.5884", "0.1285", "0.6166", "0.6815",
        "0.8959", "0.2340", "0.0520", "0.7197", "0.5311", "0.3371", "0.2905",
        "0.2422", "0.9047", "0.4137", "0.8606", "0.9463", "0.5633"
      ]
    }
  ]
}]
    })json";

TEST(PyTorchModulePredictTest,
     PredictVariedInputsMultipleModelsBatchSize1Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request1;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelVariedInputs1, register_request1)
          .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request1).ok());
  RegisterModelRequest register_request2;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelVariedInputs2, register_request2)
          .ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request2).ok());

  PredictRequest predict_request;
  predict_request.set_input(kVariedInputsMultipleModelsRequest);

  const absl::StatusOr<PredictResponse> result =
      torch_module->Predict(predict_request);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(
      result->output(),
      "{\"response\":[{\"model_path\":\"e2e_model1\",\"tensors\":[{\"tensor_"
      "shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_content\":[0."
      "5412338972091675,0.4757022559642792]}]},{\"model_path\":\"e2e_model2\","
      "\"tensors\":[{\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_"
      "content\":[0.3857767879962921,0.458008348941803]}]}]}");
}

constexpr char kStatefulModelRequest[] = R"json({
  "request" : [{
    "model_path" : "stateful_model",
    "tensors" : [
    {
      "data_type": "INT32",
      "tensor_shape": [
        1
      ],
      "tensor_content": ["0"]
    }
  ]
}]
    })json";

TEST(PyTorchModuleResetModelTest, NoResetWithStatefulModel) {
  const int kIterations = 100;
  InferenceSidecarRuntimeConfig config;
  config.set_model_reset_probability(0.0);
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kStatefulModelDir, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  for (int count = 1; count < kIterations; count++) {
    PredictRequest predict_request;
    predict_request.set_input(kStatefulModelRequest);
    absl::StatusOr predict_status = torch_module->Predict(predict_request);
    ASSERT_TRUE(predict_status.ok());
    PredictResponse response = predict_status.value();
    ASSERT_FALSE(response.output().empty());

    EXPECT_TRUE(absl::StrContains(
        response.output(),
        absl::StrCat(
            "{\"tensor_shape\":[],\"data_type\":\"INT32\",\"tensor_content\":[",
            count, "]}")))
        << response.output();
  }
}

TEST(PyTorchModuleResetModelTest, ResetSuccessWithStatefulModel) {
  const int kIterations = 10;
  InferenceSidecarRuntimeConfig config;
  config.set_model_reset_probability(1.0);
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kStatefulModelDir, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  for (int i = 0; i < kIterations; i++) {
    PredictRequest predict_request;
    predict_request.set_input(kStatefulModelRequest);
    absl::StatusOr predict_status = torch_module->Predict(predict_request);
    ASSERT_TRUE(predict_status.ok());
    PredictResponse response = predict_status.value();
    ASSERT_FALSE(response.output().empty());

    EXPECT_TRUE(absl::StrContains(
        response.output(),
        "{\"tensor_shape\":[],\"data_type\":\"INT32\",\"tensor_content\":[1]}"))
        << response.output();
    absl::SleepFor(absl::Seconds(1));
  }
}

TEST(PyTorchModuleConcurrencyTest, RegisterSameModelWithMultipleThreads) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());

  absl::BlockingCounter num_returns_ok(1);
  absl::BlockingCounter num_returns_already_exists(kNumThreads - 1);

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(
        std::thread([&torch_module, &register_request, &num_returns_ok,
                     &num_returns_already_exists]() {
          absl::StatusCode code =
              torch_module->RegisterModel(register_request).status().code();
          if (code == absl::StatusCode::kOk) {
            num_returns_ok.DecrementCount();
          } else if (code == absl::StatusCode::kAlreadyExists) {
            num_returns_already_exists.DecrementCount();
          }
        }));
    if (i == 0) {
      // The first call returns ok.
      num_returns_ok.Wait();
    }
  }
  num_returns_already_exists.Wait();

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(PyTorchModuleConcurrencyTest,
     ThreadsMakingInferenceRequestAgainstSingleModel) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kSimpleModel, register_request).ok());
  ASSERT_TRUE(torch_module->RegisterModel(register_request).ok());

  PredictRequest predict_request;
  predict_request.set_input(kSimpleRequest);

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&torch_module, &predict_request]() {
      const absl::StatusOr<PredictResponse> result =
          torch_module->Predict(predict_request);
      EXPECT_TRUE(result.ok());
      if (result.ok()) {
        EXPECT_EQ(result->output(),
                  "{\"response\":[{\"model_path\":\"simple_model\",\"tensors\":"
                  "[{\"tensor_shape\":[1],\"data_type\":\"DOUBLE\",\"tensor_"
                  "content\":[3.14]}]}]}");
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(PyTorchModuleConcurrencyTest,
     ThreadsMakingInferenceRequestAgainstMultipleModels) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> torch_module =
      ModuleInterface::Create(config);

  std::thread register_request_thread1([&torch_module]() {
    RegisterModelRequest register_request1;
    ASSERT_TRUE(
        PopulateRegisterModelRequest(kTestModelVariedInputs1, register_request1)
            .ok());
    EXPECT_TRUE(torch_module->RegisterModel(register_request1).ok());
  });

  std::thread register_request_thread2([&torch_module]() {
    RegisterModelRequest register_request2;
    ASSERT_TRUE(
        PopulateRegisterModelRequest(kTestModelVariedInputs2, register_request2)
            .ok());
    EXPECT_TRUE(torch_module->RegisterModel(register_request2).ok());
  });

  register_request_thread1.join();
  register_request_thread2.join();

  std::vector<std::thread> threads;
  PredictRequest predict_request;
  predict_request.set_input(kVariedInputsMultipleModelsRequest);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&torch_module, &predict_request]() {
      const absl::StatusOr<PredictResponse> result =
          torch_module->Predict(predict_request);
      EXPECT_TRUE(result.ok());
      if (result.ok()) {
        EXPECT_EQ(result->output(),
                  "{\"response\":[{\"model_path\":\"e2e_model1\",\"tensors\":[{"
                  "\"tensor_shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_"
                  "content\":[0.5412338972091675,0.4757022559642792]}]},{"
                  "\"model_path\":\"e2e_model2\",\"tensors\":[{\"tensor_"
                  "shape\":[2,1],\"data_type\":\"FLOAT\",\"tensor_content\":[0."
                  "3857767879962921,0.458008348941803]}]}]}");
      }
    }));
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
