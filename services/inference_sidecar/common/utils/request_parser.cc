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

#include "utils/request_parser.h"

#include <limits>
#include <utility>

#include "absl/strings/str_join.h"
#include "proto/inference_sidecar.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "src/util/status_macro/status_macros.h"
#include "utils/error.h"
#include "utils/inference_error_code.h"
#include "utils/inference_metric_util.h"
#include "utils/json_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

std::string Tensor::DebugString() const {
  std::string result;

  if (!tensor_name.empty()) {
    absl::StrAppend(&result, "Tensor name: ", tensor_name, "\n");
  }

  std::string data_type_str =
      (kDataTypeToStringMap.find(data_type) != kDataTypeToStringMap.end())
          ? kDataTypeToStringMap.at(data_type)
          : "unknown DataType";

  absl::StrAppend(&result, "Data type: ", data_type_str, "\n");

  absl::StrAppend(&result, "Tensor shape: [", absl::StrJoin(tensor_shape, ", "),
                  "]\n");

  absl::StrAppend(&result, "Tensor content: [",
                  absl::StrJoin(tensor_content, ", "), "]\n");

  return result;
}

std::string InferenceRequest::DebugString() const {
  std::string result;

  absl::StrAppend(&result, "Model path: ", model_path, "\n");
  absl::StrAppend(&result, "Tensors: [\n");

  absl::StrAppend(
      &result,
      absl::StrJoin(inputs, "\n", [](std::string* out, const Tensor& tensor) {
        absl::StrAppend(out, tensor.DebugString());
      }));

  absl::StrAppend(&result, "]\n");
  return result;
}

absl::StatusOr<DataType> StringToDataType(std::string data_type) {
  auto it = kStringToDataTypeMap.find(data_type);
  if (it != kStringToDataTypeMap.end()) {
    return it->second;
  } else {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Invalid JSON format: Unsupported 'data_type' field %s", data_type));
  }
}

// Converts a rapidjson object to a dense tensor struct.
absl::StatusOr<Tensor> ParseTensor(const rapidjson::Value& tensor_value) {
  Tensor tensor;
  // Tensor name is optional.
  PS_ASSIGN_IF_PRESENT(tensor.tensor_name, tensor_value, "tensor_name", String);

  std::string data_type;
  PS_ASSIGN_OR_RETURN(data_type, GetStringMember(tensor_value, "data_type"));

  PS_ASSIGN_OR_RETURN(tensor.data_type, StringToDataType(data_type));

  PS_ASSIGN_OR_RETURN(
      rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray tensor_shape,
      GetArrayMember(tensor_value, "tensor_shape"), _.LogError());

  // Only dense tensors are supported.
  std::size_t product_of_dimensions = 1;
  for (const rapidjson::Value& shape_value : tensor_shape) {
    int dim = shape_value.GetInt();
    if (dim < 1) {
      return absl::InvalidArgumentError(
          "Invalid tensor dimension: it has to be greater than 0");
    }
    product_of_dimensions *= dim;
    tensor.tensor_shape.push_back(dim);
  }

  PS_ASSIGN_OR_RETURN(
      rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray tensor_content,
      GetArrayMember(tensor_value, "tensor_content"), _.LogError());

  for (const rapidjson::Value& content_value : tensor_content) {
    if (!content_value.IsString()) {
      return absl::InvalidArgumentError(
          "All numbers within the tensor_content must be enclosed in quotes");
    }
    tensor.tensor_content.push_back(content_value.GetString());
  }
  std::size_t num_tensors = tensor.tensor_content.size();
  if (num_tensors != product_of_dimensions) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Mismatch between the size of tensor_content (%d) and "
                        "a product of tensor dimensions (%d)",
                        num_tensors, product_of_dimensions));
  }
  return tensor;
}

// Parses individual inference request. It takes an
// inference_request as input and populates an InferenceRequest proto and the
// model_path (for error logging purpose)
absl::StatusOr<InferenceRequest> ParseInternal(
    const rapidjson::Value& inference_request, Error& error) {
  InferenceRequest model_input;

  // Get model_path.
  PS_ASSIGN_OR_RETURN(model_input.model_path,
                      GetStringMember(inference_request, "model_path"),
                      _.LogError());
  error.model_path = model_input.model_path;

  // Get tensors.
  PS_ASSIGN_OR_RETURN(
      rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray tensors,
      GetArrayMember(inference_request, "tensors"), _.LogError());

  for (const rapidjson::Value& tensor_value : tensors) {
    PS_ASSIGN_OR_RETURN(Tensor result, ParseTensor(tensor_value), _.LogError());
    model_input.inputs.push_back(std::move(result));
  }
  return model_input;
}

absl::StatusOr<std::vector<ParsedRequestOrError>> ParseJsonInferenceRequest(
    absl::string_view json_string) {
  // TODO(b/382526825): parse async in PredictInternal()
  PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                      ParseJsonString(json_string), _.LogError());

  PS_ASSIGN_OR_RETURN(rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray
                          batch_inference_request,
                      GetArrayMember(document, "request"), _.LogError());

  std::vector<ParsedRequestOrError> parsed_inputs;
  // Extract values.
  for (const rapidjson::Value& inference_request : batch_inference_request) {
    Error error;
    absl::StatusOr<InferenceRequest> result =
        ParseInternal(inference_request, error);
    if (result.ok()) {
      parsed_inputs.push_back(ParsedRequestOrError{.request = result.value()});
    } else {
      error.description = std::string(result.status().message());
      error.error_type = Error::INPUT_PARSING;
      parsed_inputs.push_back(ParsedRequestOrError{.error = std::move(error)});
    }
  }
  // TODO(b/319500803): Implement input validation against the model signature.
  return parsed_inputs;
}

template <>
absl::StatusOr<float> Convert<float>(const std::string& str) {
  float result;
  if (absl::SimpleAtof(str, &result)) {
    return result;
  } else {
    return absl::FailedPreconditionError("Error in float conversion");
  }
}

template <>
absl::StatusOr<double> Convert<double>(const std::string& str) {
  double result;
  if (absl::SimpleAtod(str, &result)) {
    return result;
  } else {
    return absl::FailedPreconditionError("Error in double conversion");
  }
}

template <>
absl::StatusOr<int8_t> Convert<int8_t>(const std::string& str) {
  int result;
  if (absl::SimpleAtoi(str, &result)) {
    if (result < std::numeric_limits<int8_t>::min() ||
        result > std::numeric_limits<int8_t>::max()) {
      return absl::FailedPreconditionError(
          "The number is outside of bounds of int8_t.");
    }
    return result;  // Implicit conversion to int8_t.
  } else {
    return absl::FailedPreconditionError("Error in int8 conversion");
  }
}

template <>
absl::StatusOr<int16_t> Convert<int16_t>(const std::string& str) {
  int result;
  if (absl::SimpleAtoi(str, &result)) {
    if (result < std::numeric_limits<int16_t>::min() ||
        result > std::numeric_limits<int16_t>::max()) {
      return absl::FailedPreconditionError(
          "The number is outside of bounds of int16_t.");
    }
    return result;  // Implicit conversion to int16_t.
  } else {
    return absl::FailedPreconditionError("Error in int16 conversion");
  }
}

template <>
absl::StatusOr<int> Convert<int>(const std::string& str) {
  int result;
  if (absl::SimpleAtoi(str, &result)) {
    return result;
  } else {
    return absl::FailedPreconditionError("Error in int32 conversion");
  }
}

template <>
absl::StatusOr<int64_t> Convert<int64_t>(const std::string& str) {
  int64_t result;
  if (absl::SimpleAtoi(str, &result)) {
    return result;
  } else {
    return absl::FailedPreconditionError("Error in int64 conversion");
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
