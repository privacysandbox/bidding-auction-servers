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
#ifndef SERVICES_INFERENCE_SIDECAR_MODULES_TENSORFLOW_V2_14_0_TENSORFLOW_PARSER_H_
#define SERVICES_INFERENCE_SIDECAR_MODULES_TENSORFLOW_V2_14_0_TENSORFLOW_PARSER_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "tensorflow/core/framework/tensor.h"
#include "utils/error.h"
#include "utils/request_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

struct TensorWithName {
  // tensorflow::Tensor doesn't hold the name of the tensor so we need to create
  // a superset struct.
  std::string tensor_name;
  tensorflow::Tensor tensor;

  TensorWithName(std::string tensor_name, tensorflow::Tensor tensor)
      : tensor_name(tensor_name), tensor(tensor) {}
};

// Holds either a vector of tensors or an error for inference result.
struct TensorsOrError {
  std::string model_path;
  std::optional<std::vector<TensorWithName>> tensors;
  std::optional<Error> error;
};

// Transforms an internal ML framework agnostic dense tensor representation
// into a Tensorflow tensor.
absl::StatusOr<tensorflow::Tensor> ConvertFlatArrayToTensor(
    const Tensor& tensor);

// Converts inference output (Tensorflow tensors) corresponding to each model to
// a JSON string.
// batch_outputs contains a collection of <model_path, inference_output> pairs.
absl::StatusOr<std::string> ConvertTensorsOrErrorToJson(
    const std::vector<TensorsOrError>& batch_outputs);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_MODULES_TENSORFLOW_V2_14_0_TENSORFLOW_PARSER_H_
