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

#include "pytorch_parser.h"

#include <string>
#include <vector>

#include <torch/script.h>

#include "rapidjson/document.h"
#include "src/util/status_macro/status_macros.h"
#include "utils/error.h"
#include "utils/json_util.h"
#include "utils/request_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

template <typename T>
absl::StatusOr<torch::Tensor> ConvertFlatArrayToTensorInternal(
    const Tensor& tensor) {
  std::vector<T> data_array;
  for (const std::string& str : tensor.tensor_content) {
    PS_ASSIGN_OR_RETURN(T result, Convert<T>(str));
    data_array.push_back(result);
  }

  torch::Tensor py_torch_tensor =
      torch::tensor(data_array, torch::dtype<T>()).view(tensor.tensor_shape);

  return py_torch_tensor;
}

// Converts a pytorch tensor to rapidjson::Value.
absl::StatusOr<rapidjson::Value> TensorToJsonValue(
    const torch::Tensor& tensor, rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value json_tensor(rapidjson::kObjectType);

  std::vector<int64_t> tensor_shape = tensor.sizes().vec();
  rapidjson::Value tensor_shape_json(rapidjson::kArrayType);
  for (size_t i = 0; i < tensor_shape.size(); ++i) {
    tensor_shape_json.PushBack(tensor_shape[i], allocator);
  }
  json_tensor.AddMember("tensor_shape", tensor_shape_json.Move(), allocator);

  // Flatten the tensor.
  torch::Tensor reshaped_tensor = tensor.view({tensor.numel()});

  rapidjson::Value tensor_content(rapidjson::kArrayType);
  caffe2::TypeMeta dtype = tensor.dtype();
  if (dtype == torch::ScalarType::Float) {
    json_tensor.AddMember("data_type", "FLOAT", allocator);
    for (size_t i = 0; i < tensor.numel(); ++i) {
      tensor_content.PushBack(reshaped_tensor[i].item<float>(), allocator);
    }
  } else if (dtype == torch::ScalarType::Double) {
    json_tensor.AddMember("data_type", "DOUBLE", allocator);
    for (size_t i = 0; i < tensor.numel(); ++i) {
      tensor_content.PushBack(reshaped_tensor[i].item<double>(), allocator);
    }
  } else if (dtype == torch::ScalarType::Char) {
    json_tensor.AddMember("data_type", "INT8", allocator);
    for (size_t i = 0; i < tensor.numel(); ++i) {
      tensor_content.PushBack(reshaped_tensor[i].item<int8_t>(), allocator);
    }
  } else if (dtype == torch::ScalarType::Short) {
    json_tensor.AddMember("data_type", "INT16", allocator);
    for (size_t i = 0; i < tensor.numel(); ++i) {
      tensor_content.PushBack(reshaped_tensor[i].item<int16_t>(), allocator);
    }
  } else if (dtype == torch::ScalarType::Int) {
    json_tensor.AddMember("data_type", "INT32", allocator);
    for (size_t i = 0; i < tensor.numel(); ++i) {
      tensor_content.PushBack(reshaped_tensor[i].item<int>(), allocator);
    }
  } else if (dtype == torch::ScalarType::Long) {
    json_tensor.AddMember("data_type", "INT64", allocator);
    for (size_t i = 0; i < tensor.numel(); ++i) {
      tensor_content.PushBack(reshaped_tensor[i].item<int64_t>(), allocator);
    }
  } else {
    return absl::InternalError(
        absl::StrCat("Unsupported type ", std::string(dtype.name())));
  }
  json_tensor.AddMember("tensor_content", tensor_content.Move(), allocator);
  return json_tensor;
}

// Extracts pytorch tensors from inference_result and converts them to
// rapidjson::Value.
absl::StatusOr<rapidjson::Value> IValueToJsonValue(
    const std::string model_path, const torch::IValue& inference_result,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value tensors_value(rapidjson::kArrayType);

  if (inference_result.isTensor()) {
    const torch::Tensor tensor = inference_result.toTensor();
    PS_ASSIGN_OR_RETURN(rapidjson::Value json,
                        TensorToJsonValue(tensor, allocator));
    tensors_value.PushBack(json, allocator);
  } else if (inference_result.isTuple()) {
    auto output_tuple = inference_result.toTuple();
    for (int i = 0; i < output_tuple->elements().size(); i++) {
      at::Tensor tensor = output_tuple->elements()[i].toTensor();
      PS_ASSIGN_OR_RETURN(rapidjson::Value json,
                          TensorToJsonValue(tensor, allocator));
      tensors_value.PushBack(json, allocator);
    }
  } else {
    // TODO(b/329850065): Check if we need to support any additional types.
    return absl::InternalError(absl::StrCat(
        "Model ", model_path, " produces a non supported output type"));
  }
  return tensors_value;
}

}  // namespace

absl::StatusOr<torch::Tensor> ConvertFlatArrayToTensor(const Tensor& tensor) {
  switch (tensor.data_type) {
    case DataType::kFloat: {
      return ConvertFlatArrayToTensorInternal<float>(tensor);
    }
    case DataType::kDouble: {
      return ConvertFlatArrayToTensorInternal<double>(tensor);
    }
    case DataType::kInt8: {
      return ConvertFlatArrayToTensorInternal<int8_t>(tensor);
    }
    case DataType::kInt16: {
      return ConvertFlatArrayToTensorInternal<int16_t>(tensor);
    }
    case DataType::kInt32: {
      return ConvertFlatArrayToTensorInternal<int>(tensor);
    }
    case DataType::kInt64: {
      return ConvertFlatArrayToTensorInternal<int64_t>(tensor);
    }
    default:
      return absl::InvalidArgumentError(
          absl::StrFormat("Unsupported data type %d", tensor.data_type));
  }
}

absl::StatusOr<std::string> ConvertBatchOutputsToJson(
    const std::vector<PerModelOutput>& batch_outputs) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::MemoryPoolAllocator<>& allocator = document.GetAllocator();

  rapidjson::Value batch(rapidjson::kArrayType);
  for (const PerModelOutput& output : batch_outputs) {
    const std::string model_path = output.model_path;

    rapidjson::Value nested_object(rapidjson::kObjectType);
    rapidjson::Value model_path_value;
    model_path_value.SetString(model_path.c_str(), allocator);
    nested_object.AddMember("model_path", model_path_value, allocator);

    if (output.inference_output) {
      PS_ASSIGN_OR_RETURN(
          rapidjson::Value tensors_value,
          IValueToJsonValue(model_path, *output.inference_output, allocator));

      nested_object.AddMember("tensors", tensors_value.Move(), allocator);
    } else if (output.error) {
      nested_object.AddMember(
          "error", CreateSingleError(allocator, *output.error), allocator);
    }
    batch.PushBack(nested_object.Move(), allocator);
  }
  document.AddMember("response", batch.Move(), allocator);

  return SerializeJsonDoc(document);
}
}  // namespace privacy_sandbox::bidding_auction_servers::inference
