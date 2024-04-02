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

#include <utility>

#include "absl/strings/str_join.h"
#include "rapidjson/document.h"
#include "src/util/status_macro/status_macros.h"
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
  PS_ASSIGN_IF_PRESENT(tensor.tensor_name, tensor_value, "tensor_name",
                       GetString);

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

absl::StatusOr<std::vector<InferenceRequest>> ParseJsonInferenceRequest(
    absl::string_view json_string) {
  PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                      ParseJsonString(json_string), _.LogError());

  PS_ASSIGN_OR_RETURN(rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray
                          batch_inference_request,
                      GetArrayMember(document, "request"), _.LogError());

  std::vector<InferenceRequest> parsed_inputs;
  // Extract values.
  for (const rapidjson::Value& inference_request : batch_inference_request) {
    InferenceRequest model_input;

    // Get model_path.
    PS_ASSIGN_OR_RETURN(model_input.model_path,
                        GetStringMember(inference_request, "model_path"),
                        _.LogError());

    // Get tensors.
    PS_ASSIGN_OR_RETURN(
        rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray tensors,
        GetArrayMember(inference_request, "tensors"), _.LogError());
    for (const rapidjson::Value& tensor_value : tensors) {
      PS_ASSIGN_OR_RETURN(Tensor result, ParseTensor(tensor_value),
                          _.LogError());
      model_input.inputs.push_back(std::move(result));
    }
    parsed_inputs.push_back(std::move(model_input));
  }
  // TODO(b/319500803): Implement input validation against the model signature.
  return parsed_inputs;
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
