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

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::server_common {
namespace {

using google::protobuf::util::MessageDifferencer;

TEST(TelemetryFlag, Parse) {
  absl::string_view flag_str = R"pb(mode: PROD
                                    metric { name: "m_0" }
                                    metric { name: "m_1" }
                                    metric_export_interval_ms: 100
                                    dp_export_interval_ms: 200)pb";
  TelemetryFlag f_parsed;
  std::string err;
  EXPECT_TRUE(AbslParseFlag(flag_str, &f_parsed, &err));
  EXPECT_EQ(f_parsed.server_config.mode(), TelemetryConfig::PROD);
  EXPECT_EQ(f_parsed.server_config.metric_size(), 2);
  EXPECT_EQ(f_parsed.server_config.metric_export_interval_ms(), 100);
  EXPECT_EQ(f_parsed.server_config.dp_export_interval_ms(), 200);
}

TEST(TelemetryFlag, ParseError) {
  TelemetryFlag f_parsed;
  std::string err;
  EXPECT_FALSE(AbslParseFlag("cause_error", &f_parsed, &err));
}

TEST(TelemetryFlag, ParseUnParse) {
  TelemetryFlag f;
  f.server_config.set_mode(TelemetryConfig::PROD);
  f.server_config.add_metric()->set_name("m_0");
  f.server_config.add_metric()->set_name("m_1");
  TelemetryFlag f_parsed;

  std::string err;
  AbslParseFlag(AbslUnparseFlag(f), &f_parsed, &err);
  EXPECT_TRUE(
      MessageDifferencer::Equals(f.server_config, f_parsed.server_config));
}

TEST(BuildDependentConfig, Off) {
  TelemetryConfig config_proto;
  config_proto.set_mode(TelemetryConfig::OFF);
  BuildDependentConfig config(config_proto);
  EXPECT_FALSE(config.MetricAllowed());
  EXPECT_FALSE(config.TraceAllowed());
  EXPECT_FALSE(config.LogsAllowed());
}

TEST(BuildDependentConfig, Prod) {
  TelemetryConfig config_proto;
  config_proto.set_mode(TelemetryConfig::PROD);
  BuildDependentConfig config(config_proto);
  EXPECT_TRUE(config.MetricAllowed());
  EXPECT_FALSE(config.TraceAllowed());
  EXPECT_EQ(config.metric_export_interval_ms(), 60000);
  EXPECT_EQ(config.dp_export_interval_ms(), 300000);
  EXPECT_TRUE(config.LogsAllowed());
}

TEST(BuildDependentConfig, EmptyMetricConfigAlwaysOK) {
  TelemetryConfig config_proto;
  BuildDependentConfig config(config_proto);
  CHECK_OK(config.GetMetricConfig("any_metric"));
}

TEST(BuildDependentConfig, MetricConfigFilterAllowed) {
  TelemetryConfig config_proto;
  config_proto.add_metric()->set_name("allowed");
  BuildDependentConfig config(config_proto);
  EXPECT_EQ(config.GetMetricConfig("any_metric").status().code(),
            absl::StatusCode::kNotFound);
  auto metric_config = config.GetMetricConfig("allowed");
  CHECK_OK(metric_config);
  EXPECT_EQ(metric_config->name(), "allowed");
}

constexpr metric::Definition<int, metric::Privacy::kNonImpacting,
                             metric::Instrument::kUpDownCounter>
    c2("c2", "c21");
inline constexpr const metric::DefinitionName* kList[] = {&c2};

TEST(BuildDependentConfig, CheckMetricConfigInList) {
  TelemetryConfig config_proto;
  config_proto.add_metric()->set_name("c2");
  BuildDependentConfig config(config_proto);
  CHECK_OK(config.CheckMetricConfig(kList));
  config_proto.add_metric()->set_name("c3");
  config_proto.add_metric()->set_name("c4");
  BuildDependentConfig config_not_defined(config_proto);
  EXPECT_EQ(config_not_defined.CheckMetricConfig(kList).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(config_not_defined.CheckMetricConfig(kList).message(),
              testing::ContainsRegex("c3 not defined;"));
  EXPECT_THAT(config_not_defined.CheckMetricConfig(kList).message(),
              testing::ContainsRegex("c4 not defined;"));
}

}  // namespace
}  // namespace privacy_sandbox::server_common
