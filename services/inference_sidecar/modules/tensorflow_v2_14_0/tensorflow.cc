/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <istream>
#include <random>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "src/util/status_macro/status_macros.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/cc/saved_model/loader.h"
#include "tensorflow/cc/saved_model/tag_constants.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/public/session.h"
#include "tensorflow/tsl/platform/env.h"
#include "tensorflow/tsl/platform/file_system.h"
#include "utils/request_parser.h"

#include "tensorflow_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr absl::string_view kRamFileSystemScheme = "ram://";

// TODO(b/316960066): Delete all files in RamFileSystem after model loading.
// TODO(b/316960066): Move the code to a separate file with more unit tests.
absl::Status SaveToRamFileSystem(const RegisterModelRequest& request) {
  for (const auto& pair : request.model_files()) {
    const std::string& path = absl::StrCat(kRamFileSystemScheme, pair.first);
    const std::string& bytes = pair.second;
    PS_RETURN_IF_ERROR(
        tsl::WriteStringToFile(tsl::Env::Default(), path, bytes));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::pair<std::string, std::vector<TensorWithName>>>
PredictPerModel(const tensorflow::SavedModelBundle* model,
                const InferenceRequest& inference_request) {
  std::vector<std::pair<std::string, tensorflow::Tensor>> inputs;
  for (const auto& tensor : inference_request.inputs) {
    if (tensor.tensor_name.empty()) {
      return absl::InvalidArgumentError(
          "Name is required for each TensorFlow tensor input");
    }
    auto tf_tensor = ConvertFlatArrayToTensor(tensor);
    if (!tf_tensor.ok()) {
      return absl::InvalidArgumentError(tf_tensor.status().message());
    }
    inputs.emplace_back(tensor.tensor_name, *tf_tensor);
  }

  absl::string_view model_key = inference_request.model_path;
  const auto& signature_map = model->meta_graph_def.signature_def();
  if (signature_map.find("serving_default") == signature_map.end()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The 'serving_default' signature was not found for model '",
        model_key));
  }

  const auto& signature = signature_map.at("serving_default");
  std::vector<std::string> output_names;
  for (const auto& output : signature.outputs()) {
    output_names.push_back(output.second.name());
  }

  std::vector<tensorflow::Tensor> outputs;
  tensorflow::Status status =
      model->session->Run(inputs, output_names, {}, &outputs);
  if (!status.ok()) {
    return absl::InternalError(absl::StrCat(
        "Inference failed for model '", model_key, "': ", status.ToString()));
  }
  std::vector<TensorWithName> zipped_vector;
  if (output_names.size() != outputs.size()) {
    return absl::InternalError(
        "The number of output tensors doesn't match the number of output "
        "tensor names");
  }
  for (size_t i = 0; i < output_names.size(); ++i) {
    zipped_vector.push_back(TensorWithName(output_names[i], outputs[i]));
  }

  return std::make_pair(std::string(model_key), zipped_vector);
}

class TensorflowModule final : public ModuleInterface {
 public:
  explicit TensorflowModule(const InferenceSidecarRuntimeConfig& config)
      : runtime_config_(config) {}
  absl::StatusOr<PredictResponse> Predict(
      const PredictRequest& request) override ABSL_LOCKS_EXCLUDED(mu_);
  absl::StatusOr<RegisterModelResponse> RegisterModel(
      const RegisterModelRequest& request) override ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Maps each `model_path` from an inference request to its corresponding
  // tensorflow::SavedModelBundle instance.
  absl::flat_hash_map<std::string,
                      std::unique_ptr<tensorflow::SavedModelBundle>>
      model_map_ ABSL_GUARDED_BY(mu_);
  // TODO(b/327907675) : Add a test for concurrency
  absl::Mutex mu_;
  const InferenceSidecarRuntimeConfig runtime_config_;
};

absl::StatusOr<PredictResponse> TensorflowModule::Predict(
    const PredictRequest& request) {
  absl::ReaderMutexLock lock(&mu_);

  absl::StatusOr<std::vector<InferenceRequest>> parsed_requests =
      ParseJsonInferenceRequest(request.input());
  if (!parsed_requests.ok()) {
    return absl::InvalidArgumentError(parsed_requests.status().message());
  }

  std::vector<std::future<
      absl::StatusOr<std::pair<std::string, std::vector<TensorWithName>>>>>
      tasks;

  for (const InferenceRequest& inference_request : *parsed_requests) {
    absl::string_view model_key = inference_request.model_path;
    auto it = model_map_.find(model_key);
    if (it == model_map_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Requested model '", model_key, "' is not registered"));
    }
    tasks.push_back(std::async(std::launch::async, &PredictPerModel,
                               it->second.get(), inference_request));
  }

  std::vector<std::pair<std::string, std::vector<TensorWithName>>>
      batch_outputs;
  for (size_t task_id = 0; task_id < tasks.size(); ++task_id) {
    auto result_status_or = tasks[task_id].get();
    if (!result_status_or.ok()) {
      const auto& model_key = (*parsed_requests)[task_id].model_path;
      return absl::Status(result_status_or.status().code(),
                          absl::StrCat("Error during inference for model '",
                                       model_key, "', Task ID: ", task_id, ". ",
                                       result_status_or.status().message()));
    }

    batch_outputs.push_back(result_status_or.value());
  }

  auto output_json = ConvertTensorsToJson(batch_outputs);
  if (!output_json.ok()) {
    return absl::InternalError("Error during output parsing to json");
  }

  PredictResponse predict_response;
  predict_response.set_output(output_json.value());
  return predict_response;
}

absl::StatusOr<RegisterModelResponse> TensorflowModule::RegisterModel(
    const RegisterModelRequest& request) {
  tensorflow::SessionOptions session_options;
  // TODO(b/332599154): Support runtime configuration on a per-session basis.
  if (runtime_config_.num_intraop_threads() != 0) {
    session_options.config.set_intra_op_parallelism_threads(
        runtime_config_.num_intraop_threads());
  }
  if (runtime_config_.num_interop_threads() != 0) {
    session_options.config.set_inter_op_parallelism_threads(
        runtime_config_.num_interop_threads());
  }

  const std::unordered_set<std::string> tags = {"serve"};
  const auto& model_path = request.model_spec().model_path();

  if (model_path.empty()) {
    return absl::InvalidArgumentError("Model path is empty");
  }

  absl::WriterMutexLock lock(&mu_);
  auto it = model_map_.find(model_path);
  if (it != model_map_.end()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Model '", model_path, "' already registered"));
  }

  PS_RETURN_IF_ERROR(SaveToRamFileSystem(request));

  auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
  auto status = tensorflow::LoadSavedModel(
      session_options, {}, absl::StrCat(kRamFileSystemScheme, model_path), tags,
      model_bundle.get());

  if (!status.ok()) {
    return absl::InternalError(
        absl::StrCat("Error loading model: ", model_path));
  }
  model_map_[model_path] = std::move(model_bundle);
  return RegisterModelResponse();
}

}  // namespace

std::unique_ptr<ModuleInterface> ModuleInterface::Create(
    const InferenceSidecarRuntimeConfig& config) {
  return std::make_unique<TensorflowModule>(config);
}

absl::string_view ModuleInterface::GetModuleVersion() {
  return "tensorflow_v2_14_0";
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
