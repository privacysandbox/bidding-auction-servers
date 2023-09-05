/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_CONSENTED_DEBUGGING_LOGGER_H_
#define SERVICES_COMMON_UTIL_CONSENTED_DEBUGGING_LOGGER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "opentelemetry/logs/logger.h"
#include "services/common/util/context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

// Helper function to update ContextMap.
bool MaybeAddConsentedDebugConfig(const ConsentedDebugConfiguration& config,
                                  ContextLogger::ContextMap& context_map);

// Utility class to log messages based on adtech consents.
// The context including consented debugging info is cached.
class ConsentedDebuggingLogger {
 public:
  using ContextMap = ContextLogger::ContextMap;

  // TODO(b/279955398): Allows to update `context_map` via a public method.
  // TODO(b/279955398): Support opentelemtry::trace::SpanContext when
  // OpenTelemtry tracing is properly set up.
  ConsentedDebuggingLogger(const ContextMap& context_map,
                           absl::string_view server_debug_token);
  virtual ~ConsentedDebuggingLogger() = default;

  bool IsConsented() const { return is_consented_; }

  void vlog(ParamWithSourceLoc<int> verbosity_with_source_loc,
            absl::string_view msg) const;

 private:
  std::string context_;
  opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> logger_;
  // Debug token given by a consented client request.
  // A request with no consent should have no token.
  std::optional<std::string> client_debug_token_ = std::nullopt;
  // Debug token owned by the server.
  // If the consented debugging is disabled, no server debug token is set.
  std::optional<std::string> server_debug_token_ = std::nullopt;
  // True if client and server tokens match.
  bool is_consented_ = false;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_CONSENTED_DEBUGGING_LOGGER_H_
