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

namespace privacy_sandbox::bidding_auction_servers::inference {

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

}  // namespace privacy_sandbox::bidding_auction_servers::inference
