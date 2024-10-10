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

#include <memory>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "benchmark/request_utils.h"
#include "gtest/gtest.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/file_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

const int kNumThreads = 100;

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

TEST(ModuleConcurrencyTest, RegisterModel_Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> module = ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelPath, register_request).ok());
  ASSERT_TRUE(module->RegisterModel(register_request).ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&module, &register_request, i]() {
      std::string new_model_path =
          absl::StrCat(register_request.model_spec().model_path(), i);
      RegisterModelRequest new_register_request =
          CreateRegisterModelRequest(register_request, new_model_path);

      EXPECT_TRUE(module->RegisterModel(new_register_request).ok());
    }));
  }
  // Waits for all threads to finish.
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(ModuleConcurrencyTest, Predict_Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> module = ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelPath, register_request).ok());
  ASSERT_TRUE(module->RegisterModel(register_request).ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&module]() {
      PredictRequest predict_request;
      predict_request.set_input(kJsonString);
      EXPECT_TRUE(module->Predict(predict_request).ok());
    }));
  }
  // Waits for all threads to finish.
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(ModuleConcurrencyTest, RegisterModel_Predict_Success) {
  InferenceSidecarRuntimeConfig config;
  std::unique_ptr<ModuleInterface> module = ModuleInterface::Create(config);
  RegisterModelRequest register_request;
  ASSERT_TRUE(
      PopulateRegisterModelRequest(kTestModelPath, register_request).ok());
  ASSERT_TRUE(module->RegisterModel(register_request).ok());

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads * 2);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&module, &register_request, i]() {
      std::string new_model_path =
          absl::StrCat(register_request.model_spec().model_path(), i);
      RegisterModelRequest new_register_request =
          CreateRegisterModelRequest(register_request, new_model_path);

      EXPECT_TRUE(module->RegisterModel(new_register_request).ok());
    }));
  }
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&module]() {
      PredictRequest predict_request;
      // Runs concurrent predictions against a single model.
      predict_request.set_input(kJsonString);
      EXPECT_TRUE(module->Predict(predict_request).ok());
    }));
  }
  // Waits for all threads to finish.
  for (auto& thread : threads) {
    thread.join();
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
