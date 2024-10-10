//  Copyright 2024 Google LLC
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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_MODEL_VALIDATOR_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_MODEL_VALIDATOR_H_

#include <vector>

namespace privacy_sandbox::bidding_auction_servers::inference {

template <typename Node>
class GraphValidatorInterface {
 public:
  virtual ~GraphValidatorInterface() = default;

  // Checks if the graph is allowed.
  bool IsGraphAllowed() {
    for (const auto& node : GetAllNodes()) {
      if (!IsNodeAllowed(node)) {
        return false;
      }
    }
    return true;
  }

 private:
  // Checks if the node is allowed.
  bool IsNodeAllowed(const Node& node) {
    // Any operation in the denylist is disallowed.
    if (IsOpDenylisted(node)) {
      return false;
    }

    // If not stateful, it's allowed.
    if (!IsOpStateful(node)) {
      return true;
    }

    // Stateful operations require being on the allowlist.
    return IsOpAllowlisted(node);
  }

  // Returns all the nodes in the graph.
  // TODO(b/346418962): Resurrect GetNeighbors() interface if BFS traversal is
  // needed.
  // TODO(b/346418962): Resurrect GetCanonicalName() interface if deduping the
  // graph is required for optimization.
  virtual std::vector<Node> GetAllNodes() const = 0;
  // Checks if the operation is denied.
  virtual bool IsOpDenylisted(const Node& node) const = 0;
  // Checks if the operation is allowed.
  virtual bool IsOpAllowlisted(const Node& node) const {
    // Denies all stateful operations by default.
    return false;
  }

  // Checks if the operation is stateful.
  //
  // Notes:
  //  * This is an optional interface; implementations may choose not to provide
  //  it.
  //  * The definition of "stateful" can vary across ML frameworks. Generally,
  //  an
  //    operation is considered stateful if it maintains and updates internal
  //    state across multiple executions, potentially leading to non-idempotent
  //    outputs.
  virtual bool IsOpStateful(const Node& node) const {
    // By default, operations are considered not stateful.
    return false;
  }
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_MODEL_VALIDATOR_H_
