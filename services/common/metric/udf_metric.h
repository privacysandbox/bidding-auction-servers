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

#ifndef SERVICES_COMMON_METRIC_UDF_METRIC_H_
#define SERVICES_COMMON_METRIC_UDF_METRIC_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/json_util.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "src/roma/interface/roma.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kLogMetricFunctionName = "logCustomMetric";

// T is the roma meta data, must provide following API:
// absl::StatusOr<MetricContextT*> GetMetricContext()
// - MetricContextT is server_common::metrics::Context

template <typename T>
void CustomMetricCallBack(
    google::scp::roma::FunctionBindingPayload<T>& wrapper) {
  const std::string& payload = wrapper.io_proto.input_string();
  auto roma_request_context = wrapper.metadata.GetRomaRequestContext();
  auto metric_context = (*roma_request_context)->GetMetricContext();
  CHECK_OK(metric_context);
  server_common::metrics::BatchUDFMetric metrics;
  absl::Status result =
      google::protobuf::util::JsonStringToMessage(payload, &metrics);
  if (!result.ok()) {
    wrapper.io_proto.set_output_string("fail JSON parsing");
    PS_LOG(ERROR, (*roma_request_context)->GetLogContext())
        << "Failed parse BatchUDFMetric" << result.message();
    return;
  }

  absl::Status log_metric = std::visit(
      [&metrics](auto* context) { return context->LogUDFMetrics(metrics); },
      *metric_context);
  if (!log_metric.ok()) {
    wrapper.io_proto.set_output_string("fail log metric");
    PS_LOG(ERROR, (*roma_request_context)->GetLogContext())
        << "Failed call LogUDFMetrics" << log_metric.message();
    return;
  }
  wrapper.io_proto.set_output_string("log metric success");
  PS_VLOG(8, (*roma_request_context)->GetLogContext())
      << "Success log UDF metrics:\n"
      << metrics;
}

std::unique_ptr<
    google::scp::roma::FunctionBindingObjectV2<RomaRequestSharedContext>>
RegisterLogCustomMetric() {
  PS_LOG(INFO) << "Register logCustomMetric API.";
  auto log_custom_metric_binding = std::make_unique<
      google::scp::roma::FunctionBindingObjectV2<RomaRequestSharedContext>>();
  log_custom_metric_binding->function_name =
      std::string(kLogMetricFunctionName);
  log_custom_metric_binding->function =
      CustomMetricCallBack<RomaRequestSharedContext>;
  return log_custom_metric_binding;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_METRIC_UDF_METRIC_H_
