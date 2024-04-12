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

#include <future>
#include <istream>

#include <torch/script.h>

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
#include "utils/request_parser.h"

#include "pytorch_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Called on worker thread to dispatch per request inference task. It returns
// the inference result for a single request in a batched predict request.
// The forward method of a torch module is non-const although we disallow
// mutable models.
absl::StatusOr<torch::IValue> PredictInternal(torch::jit::script::Module* model,
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

class PyTorchModule final : public ModuleInterface {
 public:
  absl::StatusOr<PredictResponse> Predict(
      const PredictRequest& request) override ABSL_LOCKS_EXCLUDED(mu_);
  absl::StatusOr<RegisterModelResponse> RegisterModel(
      const RegisterModelRequest& request) override ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // The key to `model map` is the `model_path` field in an inference request.
  absl::flat_hash_map<std::string, std::unique_ptr<torch::jit::script::Module>>
      model_map_ ABSL_GUARDED_BY(mu_);
  absl::Mutex mu_;
};

absl::StatusOr<PredictResponse> PyTorchModule::Predict(
    const PredictRequest& request) {
  absl::ReaderMutexLock lock(&mu_);

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
    auto it = model_map_.find(model_key);
    if (it == model_map_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Model ", model_key, " has not been registered"));
    }
    tasks.push_back(std::async(std::launch::async, &PredictInternal,
                               it->second.get(), inference_request));
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

  absl::WriterMutexLock lock(&mu_);
  if (model_map_.find(model_key) != model_map_.end()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Model ", model_key, " has already been registered"));
  }

  // Convert PyTorch exception to absl status.
  try {
    const std::string& model_payload = request.model_files().begin()->second;
    std::istringstream is(model_payload);
    model_map_[model_key] =
        std::make_unique<torch::jit::script::Module>(torch::jit::load(is));
    // Turn on eval model for layers that behave differently during train and
    // eval times, for example, dropout and batch norm layers.
    model_map_[model_key]->eval();
  } catch (...) {
    return absl::InternalError("Error loading model");
  }

  return RegisterModelResponse();
}

}  // namespace

std::unique_ptr<ModuleInterface> ModuleInterface::Create() {
  return std::make_unique<PyTorchModule>();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
