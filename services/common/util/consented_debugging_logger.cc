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

#include "services/common/util/consented_debugging_logger.h"

#include <optional>

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/provider.h"
#include "opentelemetry/logs/severity.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/request_response_constants.h"
#include "src/cpp/util/status_macro/source_location.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

const int kTokenMinLength = 1;

std::string LogHeader(
    const ParamWithSourceLoc<int>& verbosity_with_source_loc) {
  // Example: 0 foo.cc:100]
  const server_common::SourceLocation& location =
      verbosity_with_source_loc.location;
  const int verbosity = verbosity_with_source_loc.mandatory_param;
  return absl::StrCat(verbosity, " ", location.file_name(), ":",
                      location.line(), "] ");
}

}  // namespace

bool MaybeAddConsentedDebugConfig(const ConsentedDebugConfiguration& config,
                                  ContextLogger::ContextMap& context_map) {
  if (!config.is_consented()) return false;
  absl::string_view token = config.token();
  if (token.empty()) return false;
  context_map[kToken] = std::string(token);
  return true;
}

ConsentedDebuggingLogger::ConsentedDebuggingLogger(
    const ContextMap& context_map, absl::string_view server_debug_token)
    : context_(FormatContext(context_map)),
      logger_(opentelemetry::logs::Provider::GetLoggerProvider()->GetLogger(
          "default")) {
  if (server_debug_token.length() >= kTokenMinLength) {
    server_debug_token_ = std::string(server_debug_token);
  }
  UpdateContext(context_map);
}

void ConsentedDebuggingLogger::SetContext(const ContextMap& context_map) {
  context_ = FormatContext(context_map);
  UpdateContext(context_map);
}

void ConsentedDebuggingLogger::UpdateContext(const ContextMap& context_map) {
  client_debug_token_ = std::nullopt;
  if (auto iter = context_map.find(kToken);
      iter != context_map.end() && iter->second.length() >= kTokenMinLength) {
    client_debug_token_ = std::string(iter->second);
  }
  is_consented_ = (client_debug_token_ != std::nullopt &&
                   client_debug_token_ == server_debug_token_);
}

void ConsentedDebuggingLogger::vlog(
    ParamWithSourceLoc<int> verbosity_with_source_loc,
    absl::string_view msg) const {
  // Only logs when the token in the Context matches the server token.
  if (!IsConsented()) return;
  logger_->EmitLogRecord(
      // TODO(b/279955398): Support more than one severities.
      opentelemetry::logs::Severity::kInfo,
      absl::StrCat(context_, LogHeader(verbosity_with_source_loc), msg),
      opentelemetry::common::SystemTimestamp(std::chrono::system_clock::now()));
}

}  // namespace privacy_sandbox::bidding_auction_servers
