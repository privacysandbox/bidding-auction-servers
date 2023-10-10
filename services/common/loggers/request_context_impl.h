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

#include "opentelemetry/logs/logger_provider.h"
#include "opentelemetry/logs/provider.h"
#include "services/common/loggers/request_context_logger.h"
#include "services/common/util/consented_debugging_logger.h"
#include "services/common/util/context_logger.h"

namespace privacy_sandbox::bidding_auction_servers::log {

class ContextImpl : public RequestContext {
 public:
  using ContextMap = ContextLogger::ContextMap;

  ContextImpl(const ContextMap& context_map,
              absl::string_view server_debug_token)
      : context_(FormatContext(context_map)),
        consent_logger(context_map, server_debug_token) {}

  absl::string_view ContextStr() const override { return context_; }

  bool is_consented() const override { return consent_logger.IsConsented(); }

  absl::LogSink* ConsentedSink() override { return &consented_sink; }

  bool is_debug_response() const override { return false; };

  absl::LogSink* DebugResponseSink() override { return &debug_response_sink_; };

 private:
  class ConsentedSinkImpl : public absl::LogSink {
   public:
    ConsentedSinkImpl()
        : logger_(opentelemetry::logs::Provider::GetLoggerProvider()->GetLogger(
              "default")) {}

    void Send(const absl::LogEntry& entry) override {
      logger_->EmitLogRecord(
          entry.text_message_with_prefix_and_newline_c_str());
    }
    void Flush() override {}

    opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> logger_;
  };

  // To be implemented
  class DebugResponseSinkImpl : public absl::LogSink {
   public:
    void Send(const absl::LogEntry&) override {}
    void Flush() override {}
  };

  std::string context_;
  ConsentedDebuggingLogger consent_logger;
  ConsentedSinkImpl consented_sink;
  DebugResponseSinkImpl debug_response_sink_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::log

#endif  // SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_IMPL_H_
