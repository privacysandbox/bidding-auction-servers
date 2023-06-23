// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "configure_telemetry.h"

#include <string>
#include <utility>

#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"

using ::opentelemetry::sdk::resource::Resource;
using ::opentelemetry::sdk::resource::ResourceAttributes;
namespace semantic_conventions =
    ::opentelemetry::sdk::resource::SemanticConventions;

namespace privacy_sandbox::bidding_auction_servers {

// TODO (b/278900019): set these as configurable parameters
static constexpr uint32_t kMetricsExportIntervalMillis = 60000;
static constexpr uint32_t kMetricsExportTimeoutMillis = 30000;

constexpr double kLatencyHistogramBuckets[] = {
    40'000'000,        80'000'000,     120'000'000,       160'000'000,
    220'000'000,       280'000'000,    320'000'000,       640'000'000,
    1'200'000'000,     2'500'000'000,  5'000'000'000,     10'000'000'000,
    20'000'000'000,    40'000'000'000, 80'000'000'000,    160'000'000,
    320'000'000'000,   640'000'000,    1'300'000'000'000, 2'600'000'000'000,
    5'000'000'000'000,
};

Resource CreateSharedAttributes(TrustedServerConfigUtil* config_util) {
  const auto attributes = ResourceAttributes{
      {semantic_conventions::kServiceName, config_util->GetService()},
      {semantic_conventions::kDeploymentEnvironment,
       config_util->GetEnvironment()},
      {semantic_conventions::kServiceInstanceId, config_util->GetInstanceId()}};
  return Resource::Create(attributes);
}

opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions
CreateMetricsOptions() {
  opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions
      metrics_options;

  metrics_options.export_interval_millis =
      std::chrono::milliseconds(kMetricsExportIntervalMillis);
  metrics_options.export_timeout_millis =
      std::chrono::milliseconds(kMetricsExportTimeoutMillis);

  return metrics_options;
}

std::vector<double> DefaultLatencyBuckets() {
  std::vector<double> bucket_boundaries;
  bucket_boundaries.insert(bucket_boundaries.begin(),
                           std::begin(kLatencyHistogramBuckets),
                           std::end(kLatencyHistogramBuckets));
  return bucket_boundaries;
}

}  // namespace privacy_sandbox::bidding_auction_servers
