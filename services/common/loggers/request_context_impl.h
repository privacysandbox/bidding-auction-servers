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

#ifndef SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_H_
#define SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "api/bidding_auction_servers.pb.h"
#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/provider.h"
#include "services/common/loggers/request_context_logger.h"
#include "src/cpp/util/status_macro/source_location.h"

namespace privacy_sandbox::bidding_auction_servers::log {

inline absl::string_view TokenWithMinLength(absl::string_view token) {
  constexpr int kTokenMinLength = 1;
  return token.size() < kTokenMinLength ? "" : token;
}

ABSL_CONST_INIT inline opentelemetry::logs::Logger* logger_private = nullptr;

// Utility method to format the context provided as key/value pair into a
// string. Function excludes any empty values from the output string.
std::string FormatContext(
    const absl::btree_map<std::string, std::string>& context_map);

class ContextImpl : public RequestContext {
 public:
  ContextImpl(
      const absl::btree_map<std::string, std::string>& context_map,
      absl::string_view server_debug_token,
      const ConsentedDebugConfiguration& debug_config,
      absl::AnyInvocable<DebugInfo*()> debug_info = []() { return nullptr; })
      : debug_response_sink_(std::move(debug_info)),
        server_debug_token_(TokenWithMinLength(server_debug_token)) {
    Update(context_map, debug_config);
  }

  absl::string_view ContextStr() const override { return context_; }

  void Update(const absl::btree_map<std::string, std::string>& new_context,
              const ConsentedDebugConfiguration& debug_config) {
    context_ = FormatContext(new_context);
    client_debug_token_ = debug_config.is_consented()
                              ? TokenWithMinLength(debug_config.token())
                              : "";
    debug_response_sink_.should_log_ = debug_config.is_debug_info_in_response();
  }

  bool is_consented() const override {
    return !server_debug_token_.empty() &&
           server_debug_token_ == client_debug_token_;
  }

  absl::LogSink* ConsentedSink() override { return &consented_sink; }

  bool is_debug_response() const override {
    return debug_response_sink_.ShouldLog();
  };

  absl::LogSink* DebugResponseSink() override { return &debug_response_sink_; };

 private:
  class ConsentedSinkImpl : public absl::LogSink {
   public:
    ConsentedSinkImpl() {}

    void Send(const absl::LogEntry& entry) override {
      logger_private->EmitLogRecord(
          entry.text_message_with_prefix_and_newline_c_str());
    }
    void Flush() override {}
  };

  class DebugResponseSinkImpl : public absl::LogSink {
   public:
    explicit DebugResponseSinkImpl(absl::AnyInvocable<DebugInfo*()> debug_info)
        : debug_info_(std::move(debug_info)) {}

    void Send(const absl::LogEntry& entry) override {
      debug_info_()->add_logs(entry.text_message_with_prefix());
    }

    bool ShouldLog() const { return should_log_; }

    void Flush() override {}

    absl::AnyInvocable<DebugInfo*()> debug_info_;

    bool should_log_ = false;
  };

  std::string context_;
  ConsentedSinkImpl consented_sink;
  DebugResponseSinkImpl debug_response_sink_;

  // Debug token given by a consented client request.
  std::string client_debug_token_;
  // Debug token owned by the server.
  std::string server_debug_token_;
};

template <class T>
struct ParamWithSourceLoc {
  T mandatory_param;
  server_common::SourceLocation location;
  template <class U>
  ParamWithSourceLoc(
      U param, server_common::SourceLocation loc_in PS_LOC_CURRENT_DEFAULT_ARG)
      : mandatory_param(std::forward<U>(param)), location(loc_in) {}
};

}  // namespace privacy_sandbox::bidding_auction_servers::log

#endif  // SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_H_
