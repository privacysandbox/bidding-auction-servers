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

#include <atomic>
#include <future>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "model/model_store.h"
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
#include "utils/log.h"
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
PredictPerModel(std::shared_ptr<tensorflow::SavedModelBundle> model,
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

absl::StatusOr<std::unique_ptr<tensorflow::SavedModelBundle>>
TensorFlowModelConstructor(const InferenceSidecarRuntimeConfig& config,
                           const RegisterModelRequest& request) {
  tensorflow::SessionOptions session_options;
  // TODO(b/332599154): Support runtime configuration on a per-session basis.
  if (config.num_intraop_threads() != 0) {
    session_options.config.set_intra_op_parallelism_threads(
        config.num_intraop_threads());
  }
  if (config.num_interop_threads() != 0) {
    session_options.config.set_inter_op_parallelism_threads(
        config.num_interop_threads());
  }
  const std::unordered_set<std::string> tags = {"serve"};
  const auto& model_path = request.model_spec().model_path();
  auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
  if (auto status = tensorflow::LoadSavedModel(
          session_options, {}, absl::StrCat(kRamFileSystemScheme, model_path),
          tags, model_bundle.get());
      !status.ok()) {
    return absl::InternalError(
        absl::StrCat("Error loading model: ", model_path));
  }
  return model_bundle;
}

class TensorflowModule final : public ModuleInterface {
 public:
  explicit TensorflowModule(const InferenceSidecarRuntimeConfig& config)
      : runtime_config_(config),
        store_(config, TensorFlowModelConstructor),
        background_thread_running_(true) {
    // TODO(b/332328206): Move reset model logic into the ModelStore.
    background_thread_ = std::thread([this]() {
      while (background_thread_running_) {
        // ResetModels() is continually called in the background thread.
        ResetModels();
        // Waits for the completed inference execution.
        //
        // Moves on after 1 second and check if the background thread should
        // terminate. The destructor terminates this thread by setting
        // |background_thread_running_|.
        inference_notification_.WaitForNotificationWithTimeout(
            absl::Seconds(1));
      }
    });
  }

  ~TensorflowModule() override {
    background_thread_running_ = false;
    background_thread_.join();
  }

  absl::StatusOr<PredictResponse> Predict(
      const PredictRequest& request,
      const RequestContext& request_context) override;
  absl::StatusOr<RegisterModelResponse> RegisterModel(
      const RegisterModelRequest& request) override;
  void ResetModels() override;

 private:
  const InferenceSidecarRuntimeConfig runtime_config_;

  // Stores a set of models. It's thread safe.
  // TODO(b/327907675) : Add a test for concurrency
  ModelStore<tensorflow::SavedModelBundle> store_;

  // Counts the number of inferences per model. Used for model reset.
  absl::flat_hash_map<std::string, int> per_model_inference_count_
      ABSL_GUARDED_BY(per_model_inference_count_mu_);
  absl::Mutex per_model_inference_count_mu_;

  // Notification to trigger model reset.
  absl::Notification inference_notification_;

  // The background thread continuously running ResetModels().
  std::thread background_thread_;
  // The background thread is shutdown if set to false.
  std::atomic<bool> background_thread_running_;
  // Exclusively used in a single backgrond thread. No need of a mutex.
  absl::BitGen bitgen_;
};

absl::StatusOr<PredictResponse> TensorflowModule::Predict(
    const PredictRequest& request, const RequestContext& request_context) {
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
    INFERENCE_LOG(INFO, request_context)
        << "Received inference request to model: " << model_key;
    PS_ASSIGN_OR_RETURN(std::shared_ptr<tensorflow::SavedModelBundle> model,
                        store_.GetModel(model_key, request.is_consented()));
    tasks.push_back(std::async(std::launch::async, &PredictPerModel, model,
                               inference_request));
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

  {
    absl::MutexLock lock(&per_model_inference_count_mu_);
    for (const InferenceRequest& inference_request : *parsed_requests) {
      const std::string& model_key = inference_request.model_path;
      ++per_model_inference_count_[model_key];
    }
  }
  inference_notification_.Notify();

  PredictResponse predict_response;
  predict_response.set_output(output_json.value());
  return predict_response;
}

absl::StatusOr<RegisterModelResponse> TensorflowModule::RegisterModel(
    const RegisterModelRequest& request) {
  const auto& model_path = request.model_spec().model_path();
  if (model_path.empty()) {
    return absl::InvalidArgumentError("Model path is empty");
  }

  if (store_.GetModel(model_path).ok()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Model ", model_path, " has already been registered"));
  }
  PS_RETURN_IF_ERROR(SaveToRamFileSystem(request));

  RegisterModelRequest model_request;
  *model_request.mutable_model_spec() = request.model_spec();
  PS_RETURN_IF_ERROR(store_.PutModel(model_path, model_request));
  return RegisterModelResponse();
}

// TODO(b/346813356): Refactor duplicate logic into a shared library/class.
void TensorflowModule::ResetModels() {
  const double reset_probability = runtime_config_.model_reset_probability();
  if (reset_probability == 0.0) {
    // Model reset is disabled.
    return;
  }

  std::vector<std::string> models = store_.ListModels();
  for (const auto& model_key : models) {
    int count = 0;
    {
      absl::MutexLock lock(&per_model_inference_count_mu_);
      count = per_model_inference_count_[model_key];
      // Sets the per-model counter to 0.
      per_model_inference_count_[model_key] = 0;
    }
    if (count <= 0) continue;
    double random = absl::Uniform(bitgen_, 0.0, 1.0);
    // Boosts the chance of reset multiplied by the number of inferences as
    // approximation.
    if (reset_probability != 1.0 && random >= reset_probability * count) {
      continue;
    }

    // We should make sure the model reset is successfully done.
    // Otherwise, we terminate the program to preserve user privacy.
    CHECK(store_.ResetModel(model_key).ok())
        << "Failed to reset model: " << model_key;
  }
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
