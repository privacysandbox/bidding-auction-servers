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

#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_log.h"
#include "tensorflow/core/framework/op.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

TensorFlowGraphValidator::TensorFlowGraphValidator(
    const tensorflow::GraphDef& graph)
    : graph_(graph) {
  std::vector<tensorflow::OpDef> op_list;
  tensorflow::OpRegistry::Global()->GetRegisteredOps(&op_list);
  for (const auto& op_def : op_list) {
    if (op_def.is_stateful()) {
      stateful_ops_.insert(op_def.name());
    }
  }
}

std::vector<tensorflow::NodeDef> TensorFlowGraphValidator::GetAllNodes() const {
  std::vector<tensorflow::NodeDef> nodes(graph_.node().begin(),
                                         graph_.node().end());
  for (const tensorflow::FunctionDef& func : graph_.library().function()) {
    for (const tensorflow::NodeDef& node : func.node_def()) {
      nodes.push_back(node);
    }
  }
  return nodes;
}

bool TensorFlowGraphValidator::IsOpDenylisted(
    const tensorflow::NodeDef& node) const {
  static const absl::flat_hash_set<std::string> denylisted_ops = {
      "CloseSummaryWriter",
      "CreateSummaryDbWriter",
      "CreateSummaryFileWriter",
      "FlushSummaryWriter",
      "ImportEvent",
      "StatsAggregatorSetSummaryWriter",
      "SummaryWriter",
      "WriteAudioSummary",
      "WriteFile",
      "WriteGraphSummary",
      "WriteHistogramSummary",
      "WriteImageSummary",
      "WriteRawProtoSummary",
      "WriteScalarSummary",
      "WriteSummary",
  };
  if (denylisted_ops.contains(node.op())) {
    ABSL_LOG(ERROR) << "The operation (" << node.op()
                    << ") is in the denylist.";
    return true;
  }
  return false;
}

bool TensorFlowGraphValidator::IsOpStateful(
    const tensorflow::NodeDef& node) const {
  if (stateful_ops_.contains(node.op())) {
    ABSL_LOG(ERROR) << "The operation (" << node.op() << ") is stateful.";
    return true;
  }
  return false;
}

bool TensorFlowGraphValidator::IsOpAllowlisted(
    const tensorflow::NodeDef& node) const {
  // TODO(b/368639577): Delete the following stateful ops if possible.
  static const absl::flat_hash_set<std::string> allowlisted_ops = {
      "MergeV2Checkpoints",
      "ReadVariableOp",
      "Restore",
      "RestoreV2",
      "Save",
      "SaveV2",
      "StatefulPartitionedCall",
  };
  if (allowlisted_ops.contains(node.op())) {
    ABSL_LOG(ERROR) << "The operation (" << node.op()
                    << ") is in the allowlist.";
    return true;
  }
  return false;
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
