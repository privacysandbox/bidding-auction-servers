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

#ifndef SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_PYTORCH_PARSER_H_
#define SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_PYTORCH_PARSER_H_

#include <string>
#include <utility>
#include <vector>

#include <torch/script.h>

#include "absl/status/statusor.h"
#include "utils/error.h"
#include "utils/request_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

struct PerModelOutput {
  std::string model_path;
  std::optional<torch::IValue> inference_output;
  std::optional<Error> error;
};

// Transform internal generic dense tensor representation (in the format of
// one-dimensional array) into a PyTorch tensor and the desired tensor shape.
absl::StatusOr<torch::Tensor> ConvertFlatArrayToTensor(const Tensor& tensor);

// Converts inference output corresponding to each model to a JSON string.
absl::StatusOr<std::string> ConvertBatchOutputsToJson(
    const std::vector<PerModelOutput>& batch_outputs);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_PYTORCH_PARSER_H_
