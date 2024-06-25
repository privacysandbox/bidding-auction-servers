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
#include <istream>
#include <string>
#include <thread>
#include <vector>

#include <torch/torch.h>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
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
#include "utils/log.h"
#include "utils/request_parser.h"

#include "pytorch_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Called on worker thread to dispatch per request inference task. It returns
// the inference result for a single request in a batched predict request.
// The forward method of a torch module is non-const although we disallow
// mutable models.
absl::StatusOr<torch::IValue> PredictInternal(
    std::shared_ptr<torch::jit::script::Module> model,
    const InferenceRequest& request) {
  absl::string_view model_key = request.model_path;
  std::vector<torch::jit::IValue> inputs;
  for (Tensor tensor : request.inputs) {
    const absl::StatusOr<torch::Tensor> torch_tensor =
        ConvertFlatArrayToTensor(tensor);
    if (!torch_tensor.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Model ", model_key, " encounters tensor parsing error: ",
          torch_tensor.status().message()));
    }
    inputs.push_back(*std::move(torch_tensor));
  }
  torch::IValue inference_result;
  // Convert PyTorch exception to absl status.
  try {
    // Guard against Autograd.
    c10::InferenceMode guard;
    return model->forward(inputs);
  } catch (const std::exception& e) {
    return absl::InternalError(absl::StrCat(
        "Model ", model_key,
        " encounters an exception during evaluation: ", std::string(e.what())));
  } catch (...) {
    return absl::InternalError(
        absl::StrCat("Model ", model_key,
                     " encounters an unknown exception during evaluation"));
  }
}

// Initializes PyTorch runtime inter-operations and intra-operations parallelism
// threading configurations.
absl::Status InitRuntimeThreadConfig(
    const InferenceSidecarRuntimeConfig& config) {
  try {
    // `set_num_interop_threads` needs to be called before `set_num_threads`.
    if (config.num_interop_threads() != 0) {
      at::set_num_interop_threads(config.num_interop_threads());
    }
    if (config.num_intraop_threads() != 0) {
      at::set_num_threads(config.num_intraop_threads());
    }
  } catch (const std::exception& e) {
    return absl::InternalError(absl::StrCat(
        "Error setting PyTorch threading runtime configs: ", e.what()));
  } catch (...) {
    return absl::InternalError(
        "Unknown error during while setting PyTorch threading runtime "
        "config.");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<torch::jit::script::Module>>
PyTorchModelConstructor(const InferenceSidecarRuntimeConfig& config,
                        const RegisterModelRequest& request) {
  absl::string_view model_key = request.model_spec().model_path();
  // Converts PyTorch exception to absl status.
  try {
    const std::string& model_payload = request.model_files().begin()->second;
    std::istringstream is(model_payload);
    auto model =
        std::make_unique<torch::jit::script::Module>(torch::jit::load(is));
    // Turn on eval model for layers that behave differently during train and
    // eval times, for example, dropout and batch norm layers.
    model->eval();
    return model;
  } catch (...) {
    return absl::InternalError("Error loading model");
  }
}

class PyTorchModule final : public ModuleInterface {
 public:
  explicit PyTorchModule(const InferenceSidecarRuntimeConfig& config)
      : runtime_config_(config),
        store_(config, PyTorchModelConstructor),
        background_thread_running_(true) {
    absl::Status init_result = InitRuntimeThreadConfig(config);
    CHECK(init_result.ok())
        << "Could not initialize runtime flags: " << init_result;

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

  ~PyTorchModule() override {
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
  ModelStore<torch::jit::script::Module> store_;

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

absl::StatusOr<PredictResponse> PyTorchModule::Predict(
    const PredictRequest& request, const RequestContext& request_context) {
  absl::StatusOr<std::vector<InferenceRequest>> parsed_requests =
      ParseJsonInferenceRequest(request.input());
  if (!parsed_requests.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Encounters batch inference request parsing error: ",
                     parsed_requests.status().message()));
  }

  std::vector<std::future<absl::StatusOr<torch::IValue>>> tasks;
  for (const InferenceRequest& inference_request : (*parsed_requests)) {
    absl::string_view model_key = inference_request.model_path;
    INFERENCE_LOG(INFO, request_context)
        << "Received inference request to model: " << model_key;
    PS_ASSIGN_OR_RETURN(std::shared_ptr<torch::jit::script::Module> model,
                        store_.GetModel(model_key, request.is_consented()));
    tasks.push_back(std::async(std::launch::async, &PredictInternal, model,
                               inference_request));
  }

  std::vector<PerModelOutput> batch_result_outputs;
  for (size_t task_id = 0; task_id < tasks.size(); ++task_id) {
    tasks[task_id].wait();
    // TODO(b/329280402): Handle partial results for a batched requests.
    // Currently, if some inference succeeds fails for some models but fails for
    // others, the batch result returns the error code of the first failure
    // task.
    PS_ASSIGN_OR_RETURN(torch::IValue task_result, tasks[task_id].get());
    PerModelOutput output;
    output.model_path = (*parsed_requests)[task_id].model_path;
    // TOOD(b/330899867): consider changing inference_output from torch::IValue
    // to a json value so that the conversion is done in the worker threads
    // instead of the main thread.
    output.inference_output = task_result;
    batch_result_outputs.push_back(output);
  }
  PS_ASSIGN_OR_RETURN(std::string output_json,
                      ConvertBatchOutputsToJson(batch_result_outputs));

  {
    absl::MutexLock lock(&per_model_inference_count_mu_);
    for (const InferenceRequest& inference_request : *parsed_requests) {
      const std::string& model_key = inference_request.model_path;
      ++per_model_inference_count_[model_key];
    }
  }
  inference_notification_.Notify();

  PredictResponse response;
  response.set_output(output_json);
  return response;
}

absl::StatusOr<RegisterModelResponse> PyTorchModule::RegisterModel(
    const RegisterModelRequest& request) {
  absl::string_view model_key = request.model_spec().model_path();
  if (model_key.empty()) {
    return absl::InvalidArgumentError("Empty model key during registration");
  }
  if (request.model_files().size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "The number of model files should be exactly one to match size()=",
        request.model_files().size()));
  }

  if (store_.GetModel(model_key).ok()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Model ", model_key, " has already been registered"));
  }
  PS_RETURN_IF_ERROR(store_.PutModel(model_key, request));
  return RegisterModelResponse();
}

// TODO(b/346813356): Refactor duplicate logic into a shared library/class.
void PyTorchModule::ResetModels() {
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
  return std::make_unique<PyTorchModule>(config);
}

absl::string_view ModuleInterface::GetModuleVersion() {
  return "pytorch_v2_1_1";
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
