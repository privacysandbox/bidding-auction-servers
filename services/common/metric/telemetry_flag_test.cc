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

#include "services/common/metric/telemetry_flag.h"

#include "absl/log/absl_log.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::server_common::metric {
namespace {

using google::protobuf::util::MessageDifferencer;

TEST(TelemetryFlag, Parse) {
  absl::string_view flag_str = R"pb(mode: PROD
                                    metric { name: "m_0" }
                                    metric { name: "m_1" })pb";
  TelemetryFlag f_parsed;
  std::string err;
  EXPECT_TRUE(AbslParseFlag(flag_str, &f_parsed, &err));
  EXPECT_EQ(f_parsed.server_config.mode(), ServerConfig::PROD);
  EXPECT_EQ(f_parsed.server_config.metric_size(), 2);
}

TEST(TelemetryFlag, ParseError) {
  TelemetryFlag f_parsed;
  std::string err;
  EXPECT_FALSE(AbslParseFlag("cause error", &f_parsed, &err));
}

TEST(TelemetryFlag, ParseUnParse) {
  TelemetryFlag f;
  f.server_config.set_mode(ServerConfig::PROD);
  f.server_config.add_metric()->set_name("m_0");
  f.server_config.add_metric()->set_name("m_1");
  TelemetryFlag f_parsed;

  std::string err;
  AbslParseFlag(AbslUnparseFlag(f), &f_parsed, &err);
  EXPECT_TRUE(
      MessageDifferencer::Equals(f.server_config, f_parsed.server_config));
}

TEST(BuildDependentConfig, Off) {
  ServerConfig config_proto;
  config_proto.set_mode(ServerConfig::OFF);
  BuildDependentConfig config(config_proto);
  EXPECT_FALSE(config.MetricAllowed());
  EXPECT_FALSE(config.TraceAllowed());
}

TEST(BuildDependentConfig, Prod) {
  ServerConfig config_proto;
  config_proto.set_mode(ServerConfig::PROD);
  BuildDependentConfig config(config_proto);
  EXPECT_TRUE(config.MetricAllowed());
  EXPECT_FALSE(config.TraceAllowed());
}
}  // namespace

}  // namespace privacy_sandbox::server_common::metric
