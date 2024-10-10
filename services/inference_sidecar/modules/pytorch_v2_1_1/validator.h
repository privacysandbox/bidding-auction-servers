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

#ifndef SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_VALIDATOR_H_
#define SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_VALIDATOR_H_

#include <memory>
#include <vector>

#include <torch/script.h>

#include "model/validator.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

class PyTorchGraphValidator
    : public GraphValidatorInterface<torch::jit::Node*> {
 public:
  explicit PyTorchGraphValidator(std::shared_ptr<torch::jit::Graph> graph)
      : graph_(graph) {}

 private:
  std::vector<torch::jit::Node*> GetAllNodes() const override;
  bool IsOpDenylisted(torch::jit::Node* const& node) const override;
  bool IsOpStateful(torch::jit::Node* const& node) const override;

  const std::shared_ptr<torch::jit::Graph> graph_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_VALIDATOR_H_
