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

#include <fstream>
#include <iostream>
#include <string>

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(InitTelemetry, _) {
  CommonTestInit();
  TrustedServersConfigClient config_client({});

  config_client.SetOverride("mode: OFF", TELEMETRY_CONFIG);
  config_client.SetOverride(kTrue, ENABLE_OTEL_BASED_LOGGING);
  config_client.SetOverride("", COLLECTOR_ENDPOINT);
  config_client.SetOverride("", CONSENTED_DEBUG_TOKEN);

  InitTelemetry<SelectAdRequest>(
      TrustedServerConfigUtil(/*init_config_client*/ false), config_client,
      metric::kSfe);

  // check it is Noop logger (not logging anything)
  // We only set the private logger, the shared logger is always noop
  EXPECT_TRUE(dynamic_cast<opentelemetry::logs::NoopLoggerProvider*>(
      opentelemetry::logs::Provider::GetLoggerProvider().get()));
}
TEST(CompareVersionStringTest, _) {
  std::fstream new_file;
  new_file.open("version.txt", std::ios::in);
  std::string recent_version;
  if (new_file.is_open()) {
    getline(new_file, recent_version);
    new_file.close();
  }
  EXPECT_EQ(kBuildVersion.data(), recent_version);
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
