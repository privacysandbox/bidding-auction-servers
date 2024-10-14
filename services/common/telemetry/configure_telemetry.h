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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "opentelemetry/sdk/resource/resource.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/config/trusted_server_config_client_util.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/build_info.h"
#include "src/telemetry/flag/telemetry_flag.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr std::string_view kOpenTelemetryVersion = "1.9.1";
inline constexpr std::string_view kOperator = "operator";
inline constexpr std::string_view kRegion = "region";

template <typename T>
void InitTelemetry(const TrustedServerConfigUtil& config_util,
                   const TrustedServersConfigClient& config_client,
                   absl::string_view server,
                   const std::vector<std::string>& buyer_list = {},
                   const std::vector<std::string>& model_list = {}) {
  using ::opentelemetry::logs::LoggerProvider;
  using ::opentelemetry::sdk::metrics::PeriodicExportingMetricReaderOptions;
  using ::opentelemetry::sdk::resource::Resource;
  using ::opentelemetry::sdk::resource::ResourceAttributes;
  namespace semantic_conventions =
      ::opentelemetry::sdk::resource::SemanticConventions;

  ResourceAttributes resource_attributes = {
      {semantic_conventions::kServiceName, config_util.GetService().data()},
      {semantic_conventions::kDeploymentEnvironment,
       config_util.GetEnvironment().data()},
      {semantic_conventions::kServiceInstanceId,
       config_util.GetInstanceId().data()},
      {semantic_conventions::kServiceVersion, kBuildVersion.data()},
      {kOperator.data(), config_util.GetOperator().data()},
      {kRegion.data(), config_util.GetRegion().data()}};

  absl::btree_map<std::string, std::string> zone_attribute =
      config_util.GetAttribute();
  resource_attributes.insert(zone_attribute.begin(), zone_attribute.end());

  auto telemetry_config =
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_client
              .GetCustomParameter<server_common::telemetry::TelemetryFlag>(
                  TELEMETRY_CONFIG)
              .server_config);
  std::string collector_endpoint =
      config_client.GetStringParameter(COLLECTOR_ENDPOINT).data();
  bool consented_log_enabled =
      telemetry_config->LogsAllowed() &&
      config_client.GetBooleanParameter(ENABLE_OTEL_BASED_LOGGING);
  if (consented_log_enabled) {
    server_common::log::ServerToken(
        config_client.GetStringParameter(CONSENTED_DEBUG_TOKEN));
  }
  server_common::InitTelemetry(
      config_util.GetService().data(), kOpenTelemetryVersion.data(),
      telemetry_config->TraceAllowed(), telemetry_config->MetricAllowed(),
      consented_log_enabled);
  Resource server_info = Resource::Create(resource_attributes);

  server_common::ConfigureTracer(server_info, collector_endpoint);
  static LoggerProvider* log_provider =
      server_common::ConfigurePrivateLogger(server_info, collector_endpoint)
          .release();
  server_common::log::logger_private =
      log_provider->GetLogger(config_util.GetService().data()).get();

  auto metric_export_interval =
      std::chrono::milliseconds(telemetry_config->metric_export_interval_ms());
  auto* context_map = metric::MetricContextMap<T>(
      std::move(telemetry_config),
      server_common::ConfigurePrivateMetrics(
          server_info,
          PeriodicExportingMetricReaderOptions{
              metric_export_interval,
              // use half of export interval for export_timeout_millis
              metric_export_interval / 2},
          collector_endpoint),
      config_util.GetService(), kOpenTelemetryVersion);
  AddSystemMetric(context_map);

  if constexpr (std::is_same_v<T, SelectAdRequest>) {
    AddBuyerPartition(context_map->metric_config(), buyer_list);
  }
  if constexpr (std::is_same_v<T, GenerateBidsRequest>) {
    AddModelPartition(context_map->metric_config(), model_list);
  }
  AddErrorTypePartition(context_map->metric_config(), server);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // CONFIGURE_TELEMETRY_H_
