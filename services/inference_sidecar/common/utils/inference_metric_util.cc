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

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/inference_error_code.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

void AddMetric(PredictResponse& response, const std::string& key, int32_t value,
               std::optional<std::string> partition) {
  MetricValueList& metric_list = (*response.mutable_metrics_list())[key];
  MetricValue* metric = metric_list.add_metrics();
  metric->set_value(value);
  if (partition) {
    metric->set_partition(*partition);
  }
}

std::string ExtractErrorCodeFromMessage(absl::string_view errorMessage) {
  if (absl::StartsWith(errorMessage, kInferenceTensorInputNameError)) {
    return std::string(kInferenceTensorInputNameError);
  }
  if (absl::StartsWith(errorMessage, kInferenceInputTensorConversionError)) {
    return std::string(kInferenceInputTensorConversionError);
  }
  if (absl::StartsWith(errorMessage, kInferenceSignatureNotFoundError)) {
    return std::string(kInferenceSignatureNotFoundError);
  }
  if (absl::StartsWith(errorMessage, kInferenceModelExecutionError)) {
    return std::string(kInferenceModelExecutionError);
  }
  if (absl::StartsWith(errorMessage, kInferenceOutputTensorMismatchError)) {
    return std::string(kInferenceOutputTensorMismatchError);
  }
  return std::string(
      kInferenceUnknownError);  // Return an Unknown error if no match is found
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
