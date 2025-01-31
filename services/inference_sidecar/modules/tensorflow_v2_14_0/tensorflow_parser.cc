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
#include "tensorflow_parser.h"

#include <algorithm>
#include <string>
#include <vector>

#include <rapidjson/writer.h>

#include "absl/strings/str_format.h"
#include "rapidjson/document.h"
#include "src/util/status_macro/status_macros.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "utils/error.h"
#include "utils/json_util.h"
#include "utils/request_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

template <typename T>
absl::StatusOr<tensorflow::Tensor> ConvertFlatArrayToTensorInternal(
    const Tensor& tensor) {
  tensorflow::TensorShape shape;
  for (int dim : tensor.tensor_shape) {
    shape.AddDim(dim);
  }
  tensorflow::Tensor tf_tensor(tensorflow::DataTypeToEnum<T>::v(), shape);

  std::vector<T> data_array;
  for (const std::string& str : tensor.tensor_content) {
    PS_ASSIGN_OR_RETURN(T result, Convert<T>(str));
    data_array.push_back(result);
  }
  auto tensor_data = tf_tensor.flat<T>().data();
  // Copy data from a vector to tensor's data.
  std::copy(data_array.begin(), data_array.end(), tensor_data);

  return tf_tensor;
}

absl::StatusOr<tensorflow::Tensor> ConvertFlatArrayToTensor(
    const Tensor& tensor) {
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

// Converts a single tensor to json.
absl::StatusOr<rapidjson::Value> TensorToJsonValue(
    const std::string& tensor_name, const tensorflow::Tensor& tensor,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value json_tensor(rapidjson::kObjectType);

  rapidjson::Value tensor_name_value;
  tensor_name_value.SetString(tensor_name.c_str(), allocator);
  json_tensor.AddMember("tensor_name", tensor_name_value, allocator);
  tensorflow::TensorShape tensor_shape = tensor.shape();
  rapidjson::Value tensor_shape_json(rapidjson::kArrayType);
  for (int i = 0; i < tensor_shape.dims(); ++i) {
    tensor_shape_json.PushBack(tensor_shape.dim_size(i), allocator);
  }
  json_tensor.AddMember("tensor_shape", tensor_shape_json.Move(), allocator);

  rapidjson::Value tensor_content(rapidjson::kArrayType);
  const auto data_type = tensor.dtype();
  switch (data_type) {
    case tensorflow::DataType::DT_FLOAT: {
      json_tensor.AddMember("data_type", "FLOAT", allocator);
      auto tensor_data = tensor.flat<float>();
      for (int i = 0; i < tensor_data.size(); ++i) {
        tensor_content.PushBack(tensor_data(i), allocator);
      }
      break;
    }
    case tensorflow::DataType::DT_DOUBLE: {
      json_tensor.AddMember("data_type", "DOUBLE", allocator);
      auto tensor_data = tensor.flat<double>();
      for (int i = 0; i < tensor_data.size(); ++i) {
        tensor_content.PushBack(tensor_data(i), allocator);
      }
      break;
    }
    case tensorflow::DataType::DT_INT8: {
      json_tensor.AddMember("data_type", "INT8", allocator);
      auto tensor_data = tensor.flat<int8_t>();
      for (int i = 0; i < tensor_data.size(); ++i) {
        tensor_content.PushBack(tensor_data(i), allocator);
      }
      break;
    }
    case tensorflow::DataType::DT_INT16: {
      json_tensor.AddMember("data_type", "INT16", allocator);
      auto tensor_data = tensor.flat<short>();
      for (int i = 0; i < tensor_data.size(); ++i) {
        tensor_content.PushBack(tensor_data(i), allocator);
      }
      break;
    }
    case tensorflow::DataType::DT_INT32: {
      json_tensor.AddMember("data_type", "INT32", allocator);
      auto tensor_data = tensor.flat<int>();
      for (int i = 0; i < tensor_data.size(); ++i) {
        tensor_content.PushBack(tensor_data(i), allocator);
      }
      break;
    }
    case tensorflow::DataType::DT_INT64: {
      json_tensor.AddMember("data_type", "INT64", allocator);
      auto tensor_data = tensor.flat<int64_t>();
      for (int i = 0; i < tensor_data.size(); ++i) {
        tensor_content.PushBack(tensor_data(i), allocator);
      }
      break;
    }
    default: {
      return absl::InvalidArgumentError(
          absl::StrFormat("Unsupported data type %d", data_type));
      break;
    }
  }
  json_tensor.AddMember("tensor_content", tensor_content.Move(), allocator);
  return json_tensor;
}

absl::StatusOr<std::string> ConvertTensorsOrErrorToJson(
    const std::vector<TensorsOrError>& batch_outputs) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::MemoryPoolAllocator<>& allocator = document.GetAllocator();

  rapidjson::Value batch(rapidjson::kArrayType);
  for (const auto& output : batch_outputs) {
    rapidjson::Value nested_object(rapidjson::kObjectType);
    nested_object.AddMember("model_path",
                            rapidjson::Value().SetString(
                                rapidjson::StringRef(output.model_path.data())),
                            allocator);

    if (output.tensors) {
      rapidjson::Value tensors_value(rapidjson::kArrayType);
      for (const auto& [tensor_name, tensor] : output.tensors.value()) {
        absl::StatusOr<rapidjson::Value> json =
            TensorToJsonValue(tensor_name, tensor, allocator);
        if (json.ok()) {
          tensors_value.PushBack(json.value(), allocator);
        } else {
          return json.status();
        }
      }
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
