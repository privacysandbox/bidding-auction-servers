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

#include "tensorflow.h"

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "model/model_store.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "src/util/status_macro/status_macros.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/cc/saved_model/loader.h"
#include "tensorflow/cc/saved_model/tag_constants.h"
#include "tensorflow/cc/tools/freeze_saved_model.h"
#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/protobuf/meta_graph.pb.h"
#include "tensorflow/core/public/session.h"
#include "tensorflow/tsl/platform/env.h"
#include "tensorflow/tsl/platform/file_system.h"
#include "utils/error.h"
#include "utils/inference_error_code.h"
#include "utils/inference_metric_util.h"
#include "utils/log.h"
#include "utils/request_parser.h"

#include "tensorflow_parser.h"
#include "validator.h"

ABSL_FLAG(bool, testonly_disable_model_freezing, false,
          "Disable model freezing for testing purposes.");

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

absl::StatusOr<std::vector<TensorWithName>> PredictPerModel(
    std::shared_ptr<tensorflow::SavedModelBundle> model,
    const InferenceRequest& inference_request) {
  std::vector<std::pair<std::string, tensorflow::Tensor>> inputs;
  for (const auto& tensor : inference_request.inputs) {
    if (tensor.tensor_name.empty()) {
      return absl::InvalidArgumentError(absl::StrCat(
          kInferenceTensorInputNameError,
          ". Message: ", "Name is required for each TensorFlow tensor input."));
    }
    auto tf_tensor = ConvertFlatArrayToTensor(tensor);
    if (!tf_tensor.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat(kInferenceInputTensorConversionError,
                       ". Message: ", tf_tensor.status().message()));
    }
    inputs.emplace_back(tensor.tensor_name, *tf_tensor);
  }

  absl::string_view model_key = inference_request.model_path;
  const auto& signature_map = model->meta_graph_def.signature_def();
  if (signature_map.find("serving_default") == signature_map.end()) {
    return absl::InternalError(absl::StrCat(
        kInferenceSignatureNotFoundError, ". Message: ",
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
        kInferenceModelExecutionError, ". Message: ",
        "Inference failed for model '", model_key, "': ", status.ToString()));
  }
  std::vector<TensorWithName> zipped_vector;
  if (output_names.size() != outputs.size()) {
    return absl::InternalError(
        absl::StrCat(kInferenceOutputTensorMismatchError, ". Message: ",
                     "The number of output tensors doesn't match the number of "
                     "output tensor names. "));
  }
  for (size_t i = 0; i < output_names.size(); ++i) {
    zipped_vector.push_back(TensorWithName(output_names[i], outputs[i]));
  }

  return zipped_vector;
}

absl::Status FreezeSavedModel(tensorflow::SessionOptions& session_options,
                              tensorflow::SavedModelBundle& model_bundle) {
  // TODO(b/368374975): Deprecate the absl flag at least for the prod build.
  if (absl::GetFlag(FLAGS_testonly_disable_model_freezing)) {
    return absl::OkStatus();
  }

  tensorflow::GraphDef frozen_graph_def;
  std::unordered_set<std::string> dummy_inputs, dummy_outputs;
  PS_RETURN_IF_ERROR(tensorflow::FreezeSavedModel(
      model_bundle, &frozen_graph_def, &dummy_inputs, &dummy_outputs));

  std::unique_ptr<tensorflow::Session> frozen_session(
      tensorflow::NewSession(session_options));
  PS_RETURN_IF_ERROR(frozen_session->Create(frozen_graph_def));

  // Updates the model in place.
  model_bundle.session = std::move(frozen_session);
  *model_bundle.meta_graph_def.mutable_graph_def() =
      std::move(frozen_graph_def);

  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<tensorflow::SavedModelBundle>>
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
  auto model_bundle = std::make_shared<tensorflow::SavedModelBundle>();
  if (auto status = tensorflow::LoadSavedModel(
          session_options, {}, absl::StrCat(kRamFileSystemScheme, model_path),
          tags, model_bundle.get());
      !status.ok()) {
    return absl::InternalError(
        absl::StrCat("Error loading model: ", model_path));
  }

  // TODO(b/361373900): Freeze a model only once at RegisterModel().
  if (auto status = FreezeSavedModel(session_options, *model_bundle);
      !status.ok()) {
    return absl::InternalError(absl::StrCat(
        "Error freezing model: ", model_path, ": ", status.ToString()));
  }

  // perform warm up if metadata been provided.
  // TODO(b/362338463): Add optional execute mode choice.
  if (!request.warm_up_batch_request_json().empty()) {
    absl::StatusOr<std::vector<InferenceRequest>> parsed_requests =
        ParseJsonInferenceRequest(request.warm_up_batch_request_json());
    if (!parsed_requests.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Encounters warm up batch inference request parsing error: ",
          parsed_requests.status().message()));
    }
    // Process warm up for each inference request.
    for (const InferenceRequest& inference_request : (*parsed_requests)) {
      if (inference_request.model_path != model_path) {
        return absl::InvalidArgumentError(
            "Warm up request using different model path.");
      }
      auto inference_response =
          PredictPerModel(model_bundle, inference_request);
      if (!inference_response.ok()) {
        return inference_response.status();
      }
    }
  }
  return model_bundle;
}

}  // namespace

TensorflowModule::TensorflowModule(const InferenceSidecarRuntimeConfig& config)
    : runtime_config_(config),
      store_(std::make_unique<ModelStore<tensorflow::SavedModelBundle>>(
          config, TensorFlowModelConstructor)) {}

absl::StatusOr<PredictResponse> TensorflowModule::Predict(
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

  std::vector<TensorsOrError> batch_outputs(parsed_requests->size());
  std::vector<std::future<absl::StatusOr<std::vector<TensorWithName>>>> tasks(
      parsed_requests->size());
  for (size_t task_id = 0; task_id < parsed_requests->size(); ++task_id) {
    const InferenceRequest& inference_request = (*parsed_requests)[task_id];
    const std::string& model_path = inference_request.model_path;
    INFERENCE_LOG(INFO, request_context)
        << "Received inference request to model: " << model_path;
    absl::StatusOr<std::shared_ptr<tensorflow::SavedModelBundle>> model =
        store_->GetModel(model_path, request.is_consented());
    if (!model.ok()) {
      AddMetric(predict_response, "kInferenceErrorCountByErrorCode", 1,
                std::string(kInferenceModelNotFoundError));
      INFERENCE_LOG(ERROR, request_context)
          << "Fails to get model: " << model_path
          << " Reason: " << model.status();
      batch_outputs[task_id] = TensorsOrError{
          .model_path = model_path,
          .error = Error{.error_type = Error::MODEL_NOT_FOUND,
                         .description = std::string(model.status().message())}};
    } else {
      // Only log count by model for available models since there is no metric
      // partition for unregistered models.
      AddMetric(predict_response, "kInferenceRequestCountByModel", 1,
                model_path);
      int batch_count = inference_request.inputs[0].tensor_shape[0];
      AddMetric(predict_response, "kInferenceRequestBatchCountByModel",
                batch_count, model_path);
      tasks[task_id] = std::async(std::launch::async, &PredictPerModel, *model,
                                  inference_request);
    }
  }

  for (size_t task_id = 0; task_id < parsed_requests->size(); ++task_id) {
    if (!batch_outputs[task_id].error) {
      absl::StatusOr<std::vector<TensorWithName>> tensors =
          tasks[task_id].get();
      const std::string& model_path = (*parsed_requests)[task_id].model_path;

      if (!tensors.ok()) {
        AddMetric(predict_response, "kInferenceErrorCountByErrorCode", 1,
                  std::optional(
                      ExtractErrorCodeFromMessage(tensors.status().message())));
        AddMetric(predict_response, "kInferenceRequestFailedCountByModel", 1,
                  model_path);
        INFERENCE_LOG(ERROR, request_context)
            << "Inference fails for model: " << model_path
            << " Reason: " << tensors.status();
        Error::ErrorType error_type =
            tensors.status().code() == absl::StatusCode::kInvalidArgument
                ? Error::INPUT_PARSING
                : Error::MODEL_EXECUTION;

        batch_outputs[task_id] = TensorsOrError{
            .model_path = model_path,
            .error =
                Error{.error_type = error_type,
                      .description = std::string(tensors.status().message())}};
      } else {
        int model_execution_time_ms =
            (absl::Now() - start_inference_execution_time) /
            absl::Milliseconds(1);
        AddMetric(predict_response, "kInferenceRequestDurationByModel",
                  model_execution_time_ms, model_path);
        batch_outputs[task_id] =
            TensorsOrError{.model_path = model_path, .tensors = *tensors};
      }
    }
  }

  auto output_json = ConvertTensorsOrErrorToJson(batch_outputs);
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

  predict_response.set_output(output_json.value());
  int inference_execution_time_ms =
      (absl::Now() - start_inference_execution_time) / absl::Milliseconds(1);
  AddMetric(predict_response, "kInferenceRequestDuration",
            inference_execution_time_ms);
  AddMetric(predict_response, "kInferenceResponseSize",
            predict_response.ByteSizeLong());
  return predict_response;
}

// TODO(b/346418962): Move the function into TensorFlowGraphValidator.
absl::Status IsModelAllowed(const RegisterModelRequest& request) {
  tensorflow::SessionOptions session_options;
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

  const tensorflow::GraphDef& graph_def =
      model_bundle->meta_graph_def.graph_def();
  if (!TensorFlowGraphValidator(graph_def).IsGraphAllowed()) {
    // TODO(b/368395202): Improve error messages by including the specific
    // operation that is disallowed.
    return absl::InternalError(
        absl::StrCat("Error: model ", model_path,
                     " is not allowed due to using a disallowed operator"));
  }
  return absl::OkStatus();
}

absl::StatusOr<RegisterModelResponse> TensorflowModule::RegisterModel(
    const RegisterModelRequest& request) {
  const auto& model_path = request.model_spec().model_path();
  if (model_path.empty()) {
    return absl::InvalidArgumentError("Model path is empty");
  }

  if (store_->GetModel(model_path).ok()) {
    return absl::AlreadyExistsError(
        absl::StrCat("Model ", model_path, " has already been registered"));
  }
  PS_RETURN_IF_ERROR(SaveToRamFileSystem(request));
  PS_RETURN_IF_ERROR(IsModelAllowed(request));

  RegisterModelRequest model_request;
  *model_request.mutable_model_spec() = request.model_spec();
  if (!request.warm_up_batch_request_json().empty()) {
    model_request.set_warm_up_batch_request_json(
        request.warm_up_batch_request_json());
  }
  PS_RETURN_IF_ERROR(store_->PutModel(model_path, model_request));
  return RegisterModelResponse();
}

std::unique_ptr<ModuleInterface> ModuleInterface::Create(
    const InferenceSidecarRuntimeConfig& config) {
  return std::make_unique<TensorflowModule>(config);
}

absl::string_view ModuleInterface::GetModuleVersion() {
  return "tensorflow_v2_14_0";
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
