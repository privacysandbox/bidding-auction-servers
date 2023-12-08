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

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/constants/common_service_flags.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(InitTelemetry, _) {
  TrustedServersConfigClient config_client({});

  config_client.SetFlagForTest("mode: OFF", TELEMETRY_CONFIG);
  config_client.SetFlagForTest(kTrue, ENABLE_OTEL_BASED_LOGGING);
  config_client.SetFlagForTest("", COLLECTOR_ENDPOINT);

  InitTelemetry<SelectAdRequest>(
      TrustedServerConfigUtil(/*init_config_client*/ false), config_client,
      metric::kSfe);

  // check it is Noop logger (not logging anything)
  // We only set the private logger, the shared logger is always noop
  EXPECT_TRUE(dynamic_cast<opentelemetry::logs::NoopLoggerProvider*>(
      opentelemetry::logs::Provider::GetLoggerProvider().get()));
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
