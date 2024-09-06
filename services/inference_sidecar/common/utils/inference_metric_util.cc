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

#include "utils/inference_metric_util.h"

#include "gtest/gtest.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

void AddMetric(PredictResponse& response, const std::string& key, int32_t value,
               std::optional<std::string> partition) {
  MetricValue metric;
  metric.set_value(value);
  if (partition) {
    metric.set_partition(*partition);
  }
  response.mutable_metrics()->insert({key, metric});
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
