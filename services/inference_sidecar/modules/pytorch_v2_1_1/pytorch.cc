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

#include "pytorch.h"

#include <future>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <torch/torch.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "src/util/status_macro/status_macros.h"
#include "utils/error.h"
#include "utils/inference_error_code.h"
#include "utils/inference_metric_util.h"
#include "utils/log.h"
#include "utils/request_parser.h"

#include "pytorch_parser.h"
#include "validator.h"

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
          kInferenceInputTensorConversionError, ". Message: ", "Model ",
          model_key, " encounters tensor parsing error: ",
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
        kInferenceModelExecutionError, ". Message: ", "Model ", model_key,
        " encounters an exception during evaluation: ", std::string(e.what())));
  } catch (...) {
    return absl::InternalError(absl::StrCat(
        kInferenceModelExecutionError, ". Message: ", "Model ", model_key,
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

absl::StatusOr<std::shared_ptr<torch::jit::script::Module>>
PyTorchModelConstructor(const InferenceSidecarRuntimeConfig& config,
                        const RegisterModelRequest& request) {
  torch::jit::script::Module model;
  // Converts PyTorch exception to absl status.
  try {
    const std::string& model_payload = request.model_files().begin()->second;
    std::istringstream is(model_payload);
    model = torch::jit::load(is);
    // Turn on eval model for layers that behave differently during train and
    // eval times, for example, dropout and batch norm layers.
    model.eval();
  } catch (...) {
    return absl::InternalError("Error loading model");
  }

  std::shared_ptr<torch::jit::script::Module> frozen_model;
  try {
    frozen_model =
        std::make_shared<torch::jit::script::Module>(torch::jit::freeze(model));
  } catch (...) {
    return absl::InternalError("Error encountered during model freeze");
  }

  if (frozen_model->attributes().size() > 0) {
    return absl::FailedPreconditionError("Failed to inline model graph.");
  }

  // Model warm-up.
  absl::string_view model_key = request.model_spec().model_path();
  if (!request.warm_up_batch_request_json().empty()) {
    absl::StatusOr<std::vector<InferenceRequest>> parsed_requests =
        ParseJsonInferenceRequest(request.warm_up_batch_request_json());
    if (!parsed_requests.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Encounters warm up batch inference request parsing error: ",
          parsed_requests.status().message()));
    }
    // Process warm up for each inference request.
    // TODO(b/362338463): Add optional execute mode choice.
    for (const InferenceRequest& inference_request : (*parsed_requests)) {
      if (inference_request.model_path != model_key) {
        return absl::InvalidArgumentError(
            "Warm up request using different model path.");
      }
      auto inference_response =
          PredictInternal(frozen_model, inference_request);
      if (!inference_response.ok()) {
        return inference_response.status();
      }
    }
  }
  return frozen_model;
}

}  // namespace

PyTorchModule::PyTorchModule(const InferenceSidecarRuntimeConfig& config)
    : runtime_config_(config),
      store_(std::make_unique<ModelStore<torch::jit::script::Module>>(
          config, PyTorchModelConstructor)) {
  absl::Status init_result = InitRuntimeThreadConfig(config);
  CHECK(init_result.ok()) << "Could not initialize runtime flags: "
                          << init_result;
}

absl::StatusOr<PredictResponse> PyTorchModule::Predict(
    const PredictRequest& request, const RequestContext& request_context) {
  PredictResponse predict_response;
  absl::Time start_inference_execution_time = absl::Now();
  AddMetric(predict_response, "kInferenceRequestSize", request.ByteSizeLong());
  absl::StatusOr<std::vector<InferenceRequest>> parsed_requests =
      ParseJsonInferenceRequest(request.input());
  if (!parsed_requests.ok()) {
    AddMetric(predict_response, "kInferenceErrorCountByErrorCode", 1,
              std::string(kInferenceUnableToParseRequest));
    INFERENCE_LOG(ERROR, request_context) << parsed_requests.status();
    predict_response.set_output(CreateBatchErrorString(
        Error{.error_type = Error::INPUT_PARSING,
              .description = std::string(parsed_requests.status().message())}));
    return predict_response;
  }

  AddMetric(predict_response, "kInferenceRequestCount",
            parsed_requests->size());

  std::vector<std::future<absl::StatusOr<torch::IValue>>> tasks(
      parsed_requests->size());
  std::vector<PerModelOutput> batch_outputs(parsed_requests->size());
  for (size_t task_id = 0; task_id < parsed_requests->size(); ++task_id) {
    const InferenceRequest& inference_request = (*parsed_requests)[task_id];
    const std::string& model_key = inference_request.model_path;
    INFERENCE_LOG(INFO, request_context)
        << "Received inference request to model: " << model_key;
    absl::StatusOr<std::shared_ptr<torch::jit::script::Module>> model =
        store_->GetModel(model_key, request.is_consented());
    if (!model.ok()) {
      AddMetric(predict_response, "kInferenceErrorCountByErrorCode", 1,
                std::string(kInferenceModelNotFoundError));
      INFERENCE_LOG(ERROR, request_context)
          << "Fails to get model: " << model_key
          << " Reason: " << model.status();
      batch_outputs[task_id] = PerModelOutput{
          .model_path = model_key,
          .error = Error{.error_type = Error::MODEL_NOT_FOUND,
                         .description = std::string(model.status().message())}};
    } else {
      // Only log count by model for available models since there is no metric
      // partition for unregistered models.
      AddMetric(predict_response, "kInferenceRequestCountByModel", 1,
                model_key);
      int batch_count = inference_request.inputs[0].tensor_shape[0];
      AddMetric(predict_response, "kInferenceRequestBatchCountByModel",
                batch_count, model_key);
      tasks[task_id] = std::async(std::launch::async, &PredictInternal, *model,
                                  inference_request);
    }
  }

  for (size_t task_id = 0; task_id < parsed_requests->size(); ++task_id) {
    if (!batch_outputs[task_id].error) {
      // Task launch is not blocked by a get model error.
      absl::StatusOr<torch::IValue> task_result = tasks[task_id].get();
      const std::string& model_path = (*parsed_requests)[task_id].model_path;
      if (!task_result.ok()) {
        AddMetric(predict_response, "kInferenceRequestFailedCountByModel", 1,
                  model_path);
        AddMetric(predict_response, "kInferenceErrorCountByErrorCode", 1,
                  std::optional(ExtractErrorCodeFromMessage(
                      task_result.status().message())));
        INFERENCE_LOG(ERROR, request_context)
            << "Inference fails for model: " << model_path
            << " Reason: " << task_result.status();
        Error::ErrorType error_type =
            task_result.status().code() == absl::StatusCode::kInvalidArgument
                ? Error::INPUT_PARSING
                : Error::MODEL_EXECUTION;
        batch_outputs[task_id] = PerModelOutput{
            .model_path = model_path,
            .error = Error{
                .error_type = error_type,
                .description = std::string(task_result.status().message())}};
      } else {
        int model_execution_time_ms =
            (absl::Now() - start_inference_execution_time) /
            absl::Milliseconds(1);
        AddMetric(predict_response, "kInferenceRequestDurationByModel",
                  model_execution_time_ms, model_path);
        batch_outputs[task_id] = PerModelOutput{
            .model_path = model_path, .inference_output = *task_result};
      }
    }
  }

  absl::StatusOr<std::string> output_json =
      ConvertBatchOutputsToJson(batch_outputs);
  if (!output_json.ok()) {
    AddMetric(predict_response, "kInferenceErrorCountByErrorCode", 1,
              std::string(kInferenceOutputParsingError));
    INFERENCE_LOG(ERROR, request_context) << output_json.status();
    predict_response.set_output(CreateBatchErrorString(
        Error{.error_type = Error::OUTPUT_PARSING,
              .description = "Error during output parsing to json."}));
    return predict_response;
  }

  for (const InferenceRequest& inference_request : *parsed_requests) {
    store_->IncrementModelInferenceCount(inference_request.model_path);
  }

  predict_response.set_output(*output_json);
  int inference_execution_time_ms =
      (absl::Now() - start_inference_execution_time) / absl::Milliseconds(1);
  AddMetric(predict_response, "kInferenceRequestDuration",
            inference_execution_time_ms);
  AddMetric(predict_response, "kInferenceResponseSize",
            predict_response.ByteSizeLong());
  return predict_response;
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

  if (store_->GetModel(model_key).ok()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Model ", model_key, " has already been registered"));
  }

  PS_ASSIGN_OR_RETURN(std::shared_ptr<torch::jit::script::Module> model,
                      store_->ConstructModel(request));
  if (!PyTorchGraphValidator(model->get_method("forward").graph())
           .IsGraphAllowed()) {
    return absl::InternalError(
        absl::StrCat("Error: model ", model_key,
                     " is not allowed due to using a disallowed operator"));
  }

  PS_RETURN_IF_ERROR(store_->PutModel(model_key, request));
  return RegisterModelResponse();
}

std::unique_ptr<ModuleInterface> ModuleInterface::Create(
    const InferenceSidecarRuntimeConfig& config) {
  return std::make_unique<PyTorchModule>(config);
}

absl::string_view ModuleInterface::GetModuleVersion() {
  return "pytorch_v2_1_1";
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
