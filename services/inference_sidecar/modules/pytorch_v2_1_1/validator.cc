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

#include "validator.h"

#include <vector>

#include <torch/script.h>

#include "absl/log/absl_log.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Checks if a TorchScript IR node has a subgraph.
bool HasSubgraph(torch::jit::Node* const& node) {
  for (c10::Symbol name : node->attributeNames()) {
    if (node->kindOf(name) == torch::jit::AttributeKind::g ||
        node->kindOf(name) == torch::jit::AttributeKind::gs) {
      ABSL_LOG(ERROR) << "Node : " << node->kind().toQualString()
                      << " has a subgraph. Subgraphs not supported.";
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<torch::jit::Node*> PyTorchGraphValidator::GetAllNodes() const {
  std::vector<torch::jit::Node*> nodes;
  for (const torch::jit::Node* node : graph_->get_all_nodes()) {
    nodes.push_back(const_cast<torch::jit::Node*>(node));
  }
  return nodes;
}

bool PyTorchGraphValidator::IsOpDenylisted(
    torch::jit::Node* const& node) const {
  // We currently do not allow a model with a subgraph.
  // TODO(b/355037057): Consider allowing models with subgraphs.
  return HasSubgraph(node);
}

bool PyTorchGraphValidator::IsOpStateful(torch::jit::Node* const& node) const {
  // TODO(b/355037057): Consider if we should use
  // torch::jit::Node::isNondeterministic().
  return node->hasSideEffects();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
