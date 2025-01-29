//  Copyright 2023 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_REQUEST_PARSER_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_REQUEST_PARSER_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "proto/inference_sidecar.pb.h"
#include "rapidjson/document.h"
#include "utils/error.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

enum DataType {
  kFloat,   // tf::float32, torch::ScalarType::Float (32-bit single-precision
            // floating-point number)
  kDouble,  // tf::float64, torch::ScalarType::Double (64-bit double-precision
            // floating-point number)
  kInt8,    // tf::int8 , torch::ScalarType::Char (8-bit signed integer)
  kInt16,   // tf::int16, torch::ScalarType::Short (16-bit signed integer)
  kInt32,   // tf::int32, torch::ScalarType::Int (32-bit signed integer)
  kInt64    // tf::int64, torch::ScalarType::Long (long)
};

static const std::unordered_map<std::string, DataType> kStringToDataTypeMap = {
    {"FLOAT", DataType::kFloat}, {"DOUBLE", DataType::kDouble},
    {"INT8", DataType::kInt8},   {"INT16", DataType::kInt16},
    {"INT32", DataType::kInt32}, {"INT64", DataType::kInt64}};

static const std::unordered_map<DataType, std::string> kDataTypeToStringMap = {
    {DataType::kFloat, "FLOAT"}, {DataType::kDouble, "DOUBLE"},
    {DataType::kInt8, "INT8"},   {DataType::kInt16, "INT16"},
    {DataType::kInt32, "INT32"}, {DataType::kInt64, "INT64"}};

// A struct representing a framework agnostic tensor object.
// TODO(b/319272024): Replace this struct with a native Tensor type once TF or
// Pytorch library is available in B&A.
struct Tensor {
  // Optional tensor name.
  std::string tensor_name;

  // Type of the tensor.
  DataType data_type;

  // Tensor shape.
  // The order of entries matters: It indicates the layout of the
  // values in the tensor in-memory representation.
  //
  // The first entry is the outermost dimension. The last entry is the
  // innermost dimension.
  std::vector<int64_t> tensor_shape;

  // Serialized raw tensor content. It holds the flattened representation of
  // the tensor. Only the representation corresponding to "data_type" field can
  // be set. The number of elements in tensor_content should be equal to
  // the product of tensor_shape elements, for example
  // a tensor of shape [1,4] will expect a flat array or 4 elements
  // (e.g. [1, 2, 7, 4]) and one with a shape [2,3] will expect a 6 element one.
  std::vector<std::string> tensor_content;

  std::string DebugString() const;
};

// A struct representing input data for a specific Ads ML model.
// TODO(b/323592662): Use a proto instead of a C++ struct.
struct InferenceRequest {
  // A unique path to the model.
  std::string model_path;

  // A vector of input tensors.
  std::vector<Tensor> inputs;

  std::string DebugString() const;
};

// A struct representing parsed inference request or parsing error
struct ParsedRequestOrError {
  std::optional<InferenceRequest> request;
  std::optional<Error> error;
};

// Validates and parses JSON inference request to an internal data structure.
// For a practical example, see generateBidRunInference.js.
absl::StatusOr<std::vector<ParsedRequestOrError>> ParseJsonInferenceRequest(
    absl::string_view json_string);

template <typename T>
absl::StatusOr<T> Convert(const std::string& str);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_REQUEST_PARSER_H_
