// Copyright 2023 Google LLC
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

#include "services/common/telemetry/telemetry_flag.h"

#include "absl/strings/str_cat.h"
#include "google/protobuf/text_format.h"

namespace privacy_sandbox::server_common {
namespace {

template <typename T>
inline absl::StatusOr<T> ParseText(absl::string_view text) {
  T message;
  if (!google::protobuf::TextFormat::ParseFromString(text.data(), &message)) {
    return absl::InvalidArgumentError(
        absl::StrCat("invalid proto format:{", text, "}"));
  }
  return message;
}

}  // namespace

bool AbslParseFlag(absl::string_view text, TelemetryFlag* flag,
                   std::string* err) {
  absl::StatusOr<TelemetryConfig> s = ParseText<TelemetryConfig>(text);
  if (!s.ok()) {
    *err = s.status().message();
    return false;
  }
  flag->server_config = *s;
  return true;
}

std::string AbslUnparseFlag(const TelemetryFlag& flag) {
  return flag.server_config.ShortDebugString();
}

BuildDependentConfig::BuildDependentConfig(TelemetryConfig config)
    : server_config_(std::move(config)) {
  if (server_config_.metric_export_interval_ms() == 0) {
    constexpr int kDefaultMetricExport = 60'000;
    server_config_.set_metric_export_interval_ms(kDefaultMetricExport);
  }
  if (server_config_.dp_export_interval_ms() == 0) {
    constexpr int kDefaultDpExport = 300'000;
    server_config_.set_dp_export_interval_ms(kDefaultDpExport);
  }
  // dp_export_interval_ms should be at least metric_export_interval_ms
  if (server_config_.metric_export_interval_ms() >
      server_config_.dp_export_interval_ms()) {
    server_config_.set_dp_export_interval_ms(
        server_config_.metric_export_interval_ms());
  }
  for (const MetricConfig& m : server_config_.metric()) {
    metric_config_.emplace(m.name(), m);
  }
}

TelemetryConfig::TelemetryMode BuildDependentConfig::MetricMode() const {
  if (GetBuildMode() == BuildMode::kExperiment) {
    return server_config_.mode();
  } else {
    return server_config_.mode() == TelemetryConfig::OFF
               ? TelemetryConfig::OFF
               : TelemetryConfig::PROD;
  }
}

bool BuildDependentConfig::MetricAllowed() const {
  switch (server_config_.mode()) {
    case TelemetryConfig::PROD:
    case TelemetryConfig::EXPERIMENT:
    case TelemetryConfig::COMPARE:
      return true;
    default:
      return false;
  }
}

bool BuildDependentConfig::IsDebug() const {
  switch (server_config_.mode()) {
    case TelemetryConfig::EXPERIMENT:
    case TelemetryConfig::COMPARE:
      return GetBuildMode() == BuildMode::kExperiment;
    default:
      return false;
  }
}

absl::StatusOr<MetricConfig> BuildDependentConfig::GetMetricConfig(
    absl::string_view metric_name) const {
  if (metric_config_.empty()) {
    return MetricConfig();
  }
  auto it = metric_config_.find(metric_name);
  if (it == metric_config_.end()) {
    return absl::NotFoundError(metric_name);
  }
  return it->second;
}

absl::Status BuildDependentConfig::CheckMetricConfig(
    const absl::Span<const metric::DefinitionName* const>& server_metrics)
    const {
  std::string ret;
  for (const auto& [name, config] : metric_config_) {
    if (absl::c_find_if(server_metrics, [&name = name](const auto* metric_def) {
          return metric_def->name_ == name;
        }) == server_metrics.end()) {
      absl::StrAppend(&ret, absl::StrCat(name, " not defined;"));
    }
  }
  return ret.empty() ? absl::OkStatus() : absl::InvalidArgumentError(ret);
}

}  // namespace privacy_sandbox::server_common
