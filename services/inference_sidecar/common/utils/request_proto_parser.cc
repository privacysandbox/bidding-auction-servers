// Copyright 2025 Google LLC
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
#include "utils/request_proto_parser.h"

#include <limits>
#include <utility>

#include <rapidjson/writer.h>

#include "absl/strings/str_join.h"
#include "proto/inference_payload.pb.h"
#include "rapidjson/document.h"
#include "src/util/status_macro/status_macros.h"
#include "utils/json_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

absl::StatusOr<DataTypeProto> StringToDataType(std::string data_type) {
  auto it = kStringToDataTypeProtoMap.find(data_type);
  if (it != kStringToDataTypeProtoMap.end()) {
    return it->second;
  } else {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Invalid JSON format: Unsupported 'data_type' field %s", data_type));
  }
}

absl::Status AddTensorContent(const std::string& value, TensorProto& tensor) {
  switch (tensor.data_type()) {
    case DataTypeProto::FLOAT:
      float tensor_content_float;
      if (absl::SimpleAtof(value, &tensor_content_float)) {
        tensor.mutable_tensor_content()->add_tensor_content_float(
            tensor_content_float);
      } else {
        return absl::FailedPreconditionError("Error in float conversion");
      }
      break;
    case DataTypeProto::DOUBLE:
      double tensor_content_double;
      if (absl::SimpleAtod(value, &tensor_content_double)) {
        tensor.mutable_tensor_content()->add_tensor_content_double(
            tensor_content_double);
      } else {
        return absl::FailedPreconditionError("Error in double conversion");
      }
      break;
    case DataTypeProto::INT32:
      int tensor_content_int32;
      if (absl::SimpleAtoi(value, &tensor_content_int32)) {
        tensor.mutable_tensor_content()->add_tensor_content_int32(
            tensor_content_int32);
      } else {
        return absl::FailedPreconditionError("Error in int32 conversion");
      }
      break;
    case DataTypeProto::INT16:
      int tensor_content_int16;
      if (absl::SimpleAtoi(value, &tensor_content_int16)) {
        if (tensor_content_int16 < std::numeric_limits<int16_t>::min() ||
            tensor_content_int16 > std::numeric_limits<int16_t>::max()) {
          return absl::FailedPreconditionError(
              "The number is outside of bounds of int16_t.");
        }
        tensor.mutable_tensor_content()->add_tensor_content_int32(
            tensor_content_int16);
      } else {
        return absl::FailedPreconditionError("Error in int16 conversion");
      }
      break;
    case DataTypeProto::INT8:
      int tensor_content_int8;
      if (absl::SimpleAtoi(value, &tensor_content_int8)) {
        if (tensor_content_int8 < std::numeric_limits<int8_t>::min() ||
            tensor_content_int8 > std::numeric_limits<int8_t>::max()) {
          return absl::FailedPreconditionError(
              "The number is outside of bounds of int8_t.");
        }
        tensor.mutable_tensor_content()->add_tensor_content_int32(
            tensor_content_int8);
      } else {
        return absl::FailedPreconditionError("Error in int8 conversion");
      }
      break;
    case DataTypeProto::INT64:
      int64_t tensor_content_int64;
      if (absl::SimpleAtoi(value, &tensor_content_int64)) {
        tensor.mutable_tensor_content()->add_tensor_content_int64(
            tensor_content_int64);
      } else {
        return absl::FailedPreconditionError("Error in int64 conversion");
      }
      break;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported data type: ", tensor.data_type()));
  }
  return absl::OkStatus();
}

// Converts a rapidjson object to a dense tensor struct.
absl::StatusOr<TensorProto> ParseTensorProto(
    const rapidjson::Value& tensor_value) {
  TensorProto tensor;
  // Tensor name is optional.
  std::string tensor_name_val;
  PS_ASSIGN_IF_PRESENT(tensor_name_val, tensor_value, "tensor_name", String);
  tensor.set_tensor_name(tensor_name_val);
  PS_ASSIGN_OR_RETURN(std::string data_type,
                      GetStringMember(tensor_value, "data_type"));
  DataTypeProto data_type_val;
  PS_ASSIGN_OR_RETURN(data_type_val, StringToDataType(data_type));
  tensor.set_data_type(data_type_val);
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
    tensor.add_tensor_shape(dim);
  }
  PS_ASSIGN_OR_RETURN(
      rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray tensor_content,
      GetArrayMember(tensor_value, "tensor_content"), _.LogError());
  std::size_t num_tensors = 0;
  for (const rapidjson::Value& content_value : tensor_content) {
    if (!content_value.IsString()) {
      return absl::InvalidArgumentError(
          "All numbers within the tensor_content must be enclosed in quotes");
    }
    absl::Status status = AddTensorContent(content_value.GetString(), tensor);
    if (!status.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(status.message()));
    }
    num_tensors = num_tensors + 1;
  }
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
absl::StatusOr<InferenceRequestProto> ParseInternal(
    const rapidjson::Value& inference_request, ErrorProto& error) {
  InferenceRequestProto model_input;
  // Get model_path.
  PS_ASSIGN_OR_RETURN(std::string model_path,
                      GetStringMember(inference_request, "model_path"),
                      _.LogError());
  model_input.set_model_path(model_path);
  error.set_model_path(model_path);  // add model path to error
  // Get tensors.
  PS_ASSIGN_OR_RETURN(
      rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray tensors,
      GetArrayMember(inference_request, "tensors"), _.LogError());
  for (const rapidjson::Value& tensor_value : tensors) {
    PS_ASSIGN_OR_RETURN(TensorProto result, ParseTensorProto(tensor_value),
                        _.LogError());
    *model_input.add_tensors() = result;
  }
  return model_input;
}

absl::StatusOr<BatchInferenceRequest> ConvertJsonToProto(
    absl::string_view json_string,
    BatchOrderedInferenceErrorResponse& parsing_errors) {
  BatchInferenceRequest parsed_batch_input;
  PS_ASSIGN_OR_RETURN(rapidjson::Document document,
                      ParseJsonString(json_string), _.LogError());
  PS_ASSIGN_OR_RETURN(rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray
                          batch_inference_request,
                      GetArrayMember(document, "request"), _.LogError());
  //  Extract values.
  for (int64_t i = 0; i < batch_inference_request.Size(); ++i) {
    const rapidjson::Value& inference_request = batch_inference_request[i];
    ErrorProto error;
    absl::StatusOr<InferenceRequestProto> result =
        ParseInternal(inference_request, error);
    if (result.ok()) {
      *parsed_batch_input.add_request() = std::move(*result);
    } else {
      error.set_description(std::string(result.status().message()));
      error.set_error_type(ErrorProto::INPUT_PARSING);
      OrderedInferenceErrorResponse& indexed_response =
          *parsing_errors.add_response();
      indexed_response.set_index(i);
      indexed_response.mutable_response()->set_model_path(error.model_path());
      *indexed_response.mutable_response()->mutable_error() = std::move(error);
    }
  }
  if (parsed_batch_input.request_size() == 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("All requests are invalid"));
  }
  // TODO(b/319500803): Implement input validation against the model signature.
  return parsed_batch_input;
}

absl::StatusOr<rapidjson::Value> TensorProtoToJsonValue(
    const TensorProto& tensor, rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value json_tensor(rapidjson::kObjectType);
  rapidjson::Value tensor_name_value;
  tensor_name_value.SetString(tensor.tensor_name().c_str(), allocator);
  json_tensor.AddMember("tensor_name", tensor_name_value, allocator);
  rapidjson::Value tensor_shape_json(rapidjson::kArrayType);
  for (const int shape : tensor.tensor_shape()) {
    tensor_shape_json.PushBack(shape, allocator);
  }
  json_tensor.AddMember("tensor_shape", tensor_shape_json.Move(), allocator);
  rapidjson::Value tensor_content(rapidjson::kArrayType);
  const auto data_type = tensor.data_type();
  switch (data_type) {
    case DataTypeProto::FLOAT: {
      json_tensor.AddMember("data_type", "FLOAT", allocator);
      for (const auto data : tensor.tensor_content().tensor_content_float()) {
        tensor_content.PushBack(data, allocator);
      }
      break;
    }
    case DataTypeProto::DOUBLE: {
      json_tensor.AddMember("data_type", "DOUBLE", allocator);
      for (const auto data : tensor.tensor_content().tensor_content_double()) {
        tensor_content.PushBack(data, allocator);
      }
      break;
    }
    case DataTypeProto::INT8: {
      json_tensor.AddMember("data_type", "INT8", allocator);
      for (const auto data : tensor.tensor_content().tensor_content_int32()) {
        tensor_content.PushBack(data, allocator);
      }
      break;
    }
    case DataTypeProto::INT16: {
      json_tensor.AddMember("data_type", "INT16", allocator);
      for (const auto data : tensor.tensor_content().tensor_content_int32()) {
        tensor_content.PushBack(data, allocator);
      }
      break;
    }
    case DataTypeProto::INT32: {
      json_tensor.AddMember("data_type", "INT32", allocator);
      for (const auto data : tensor.tensor_content().tensor_content_int32()) {
        tensor_content.PushBack(data, allocator);
      }
      break;
    }
    case DataTypeProto::INT64: {
      json_tensor.AddMember("data_type", "INT64", allocator);
      for (const auto data : tensor.tensor_content().tensor_content_int64()) {
        tensor_content.PushBack(data, allocator);
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

rapidjson::Value ConvertErrorProtoToJson(
    rapidjson::Document::AllocatorType& allocator, const ErrorProto& error) {
  rapidjson::Value error_object(rapidjson::kObjectType);
  // model path
  std::string error_type_str = ErrorProto_ErrorType_Name(error.error_type());
  error_object.AddMember(
      "error_type",
      rapidjson::Value().SetString(rapidjson::StringRef(error_type_str.c_str()),
                                   allocator),
      allocator);
  if (error.description().size() > 0) {
    error_object.AddMember(
        "description",
        rapidjson::Value().SetString(
            rapidjson::StringRef(error.description().c_str()), allocator),
        allocator);
  }
  return error_object;
}

absl::StatusOr<std::string> ConvertProtoToJson(
    const BatchInferenceResponse& response) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::MemoryPoolAllocator<>& allocator = document.GetAllocator();
  rapidjson::Value batch(rapidjson::kArrayType);
  for (const InferenceResponseProto& output : response.response()) {
    rapidjson::Value nested_object(rapidjson::kObjectType);
    nested_object.AddMember("model_path",
                            rapidjson::Value().SetString(rapidjson::StringRef(
                                output.model_path().c_str())),
                            allocator);
    if (output.tensors().size() > 0) {
      rapidjson::Value tensors_value(rapidjson::kArrayType);
      for (const auto& tensor : output.tensors()) {
        absl::StatusOr<rapidjson::Value> json =
            TensorProtoToJsonValue(tensor, allocator);  // wip
        if (json.ok()) {
          tensors_value.PushBack(json.value(), allocator);
        } else {
          return json.status();
        }
      }
      nested_object.AddMember("tensors", tensors_value.Move(), allocator);
    } else if (output.has_error()) {
      nested_object.AddMember(
          "error", ConvertErrorProtoToJson(allocator, output.error()),
          allocator);
    }
    batch.PushBack(nested_object.Move(), allocator);
  }
  document.AddMember("response", batch.Move(), allocator);
  return SerializeJsonDoc(document);
}

absl::StatusOr<BatchInferenceResponse> MergeBatchResponse(
    const BatchInferenceResponse& response,
    const BatchOrderedInferenceErrorResponse& parsing_errors) {
  // If there is no parsing error, return the original response
  if (parsing_errors.response_size() == 0) {
    return response;
  }
  std::vector<InferenceResponseProto> all_responses(
      parsing_errors.response_size() + response.response_size());
  for (const OrderedInferenceErrorResponse& indexed_response :
       parsing_errors.response()) {
    int index = indexed_response.index();
    if (index >= 0 && index < all_responses.size()) {
      all_responses[index] = std::move(indexed_response.response());
    } else {
      return absl::InvalidArgumentError(absl::StrFormat(
          "OrderedInferenceErrorResponse with out-of-bounds index"));
    }
  }
  int non_indexed_index = 0;
  for (int i = 0; i < all_responses.size(); ++i) {
    if (all_responses[i].ByteSizeLong() == 0) {
      if (non_indexed_index < response.response_size()) {
        all_responses[i] = std::move(response.response(non_indexed_index));
        non_indexed_index++;
      }
    }
  }
  BatchInferenceResponse merged_response;
  for (InferenceResponseProto& response : all_responses) {
    merged_response.mutable_response()->Add(std::move(response));
  }
  return merged_response;
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
