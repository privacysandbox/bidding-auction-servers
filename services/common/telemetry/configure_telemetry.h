/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CONFIGURE_TELEMETRY_H_
#define CONFIGURE_TELEMETRY_H_

#include <string>
#include <vector>

#include "opentelemetry/sdk/resource/resource.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "src/cpp/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

// TODO (b/278899152): get version dynamically
constexpr std::string_view kOpenTelemetryVersion = "1.8.2";

// Creates the attributes that all metrics/traces, regardless of the context
// they are emitted, will have assigned.
// config_util: the B&A configuration utility
opentelemetry::sdk::resource::Resource CreateSharedAttributes(
    TrustedServerConfigUtil* config_util);

// Creates the metrics collection options for OpenTelemtry configuration.
opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions
CreateMetricsOptions();

// Returns a double vector of the default histogram buckets for latency
std::vector<double> DefaultLatencyBuckets();

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // CONFIGURE_TELEMETRY_H_
