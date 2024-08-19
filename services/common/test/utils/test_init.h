/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_TEST_UTILS_TEST_INIT_H_
#define SERVICES_COMMON_TEST_UTILS_TEST_INIT_H_

#include <memory>

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/string_view.h"
#include "services/common/telemetry/configure_telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

void CommonTestInit(absl::string_view test_name = "bna_test") {
  absl::InitializeSymbolizer(test_name.data());
  absl::FailureSignalHandlerOptions options;
  absl::InstallFailureSignalHandler(options);

  static opentelemetry::logs::LoggerProvider* log_provider =
      std::make_unique<opentelemetry::logs::NoopLoggerProvider>().release();
  server_common::log::logger_private =
      log_provider->GetLogger(test_name.data()).get();

  // Set verbosity
  server_common::log::SetGlobalPSVLogLevel(10);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_UTILS_TEST_INIT_H_
