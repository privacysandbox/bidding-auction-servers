//  Copyright 2022 Google LLC
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

#include "services/common/metric/metric_router.h"

#include <utility>
#include <vector>

#include "absl/log/absl_log.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/aggregation/histogram_aggregation.h"
#include "opentelemetry/sdk/metrics/meter.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "services/common/metric/definition.h"
#include "services/common/util/read_system.h"
#include "services/common/util/status_macros.h"

namespace privacy_sandbox::server_common::metric {
namespace sdk = ::opentelemetry::sdk::metrics;
namespace api = ::opentelemetry::metrics;

MetricRouter::MetricRouter(Meter* meter, PrivacyBudget fraction,
                           absl::Duration dp_output_period)
    : meter_(meter), dp_(this, fraction, dp_output_period) {
  if (meter_ != nullptr) {
    AddObserverable(kCpuPercent, GetCpu);
    AddObserverable(kMemoryKB, GetMemory);
    return;
  }
  ABSL_LOG(ERROR) << "meter is null at initializing, init with default";
  api::Provider::SetMeterProvider((std::shared_ptr<api::MeterProvider>)
                                      std::make_shared<sdk::MeterProvider>());
  meter_ = api::Provider::GetMeterProvider()
               ->GetMeter("default initialized", "default initialized")
               .get();
}

void MetricRouter::AddHistogramView(absl::string_view instrument_name,
                                    const internal::Histogram& histogram) {
  auto aggregation_config = std::make_shared<sdk::HistogramAggregationConfig>();
  aggregation_config->boundaries_ =
      std::vector<double>(histogram.histogram_boundaries_.begin(),
                          histogram.histogram_boundaries_.end());
  auto* sdk_meter = static_cast<sdk::Meter*>(meter_);
  static_cast<sdk::MeterProvider*>(api::Provider::GetMeterProvider().get())
      ->AddView(
          std::make_unique<sdk::InstrumentSelector>(
              sdk::InstrumentType::kHistogram, instrument_name.data()),
          std::make_unique<sdk::MeterSelector>(
              sdk_meter->GetInstrumentationScope()->GetName(),
              sdk_meter->GetInstrumentationScope()->GetVersion(),
              sdk_meter->GetInstrumentationScope()->GetSchemaURL()),
          // First 2 arguments use empty string, so not to overwrite
          // instrument's name and description
          std::make_unique<sdk::View>("", "", sdk::AggregationType::kHistogram,
                                      aggregation_config));
}

}  // namespace privacy_sandbox::server_common::metric
