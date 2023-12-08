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

#ifndef SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_LOGGER_H_
#define SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_LOGGER_H_

#include <optional>

#include "absl/log/absl_log.h"
#include "absl/log/log_sink.h"

namespace privacy_sandbox::bidding_auction_servers::log {

// Similar to absl VLOG_IS_ON. calling first time set the max_verbosity by
// `max_v`, after first time `max_v` is ignored.
inline bool PS_VLOG_IS_ON(int verbose_level,
                          std::optional<int> max_v = std::nullopt) {
  static int max_verbosity = [max_v]() {
    if (max_v == std::nullopt) {
      fprintf(stderr,
              "Warning: verbosity is not set, PS_VLOG is turned off.\n");
      return 0;
    } else {
      return *max_v;
    }
  }();
  return verbose_level <= max_verbosity;
}

// Used by `PS_VLOG`, to provide the context of how to log a request.
class RequestContext {
 public:
  // `ContextStr()` will be added to the front of log message.
  virtual absl::string_view ContextStr() const = 0;
  // if `is_consented()`, `ConsentedSink()` will log
  virtual bool is_consented() const = 0;
  virtual absl::LogSink* ConsentedSink() = 0;
  // if `is_debug_response()`, `DebugResponseSink()` will log
  virtual bool is_debug_response() const = 0;
  virtual absl::LogSink* DebugResponseSink() = 0;
};

class NoOpContext : public RequestContext {
 public:
  absl::string_view ContextStr() const override { return ""; }
  bool is_consented() const override { return false; }
  absl::LogSink* ConsentedSink() override { return nullptr; }
  bool is_debug_response() const override { return false; }
  absl::LogSink* DebugResponseSink() override { return nullptr; }
};

inline constexpr NoOpContext kNoOpContext;

// Extend LogMessage to be able to conditionally add LogSink
class PSLogMessage : public absl::log_internal::LogMessage {
 public:
  using LogMessage::LogMessage;

  PSLogMessage& ToSinkAlsoIf(bool condition, absl::LogSink* sink) {
    if (condition) {
      ToSinkAlso(sink);
    }
    return *this;
  }
};

}  // namespace privacy_sandbox::bidding_auction_servers::log

#ifdef PS_LOG_NON_PROD
#define PS_VLOG_INTERNAL(verbose_level, request_context)                       \
  switch (::privacy_sandbox::bidding_auction_servers::log::                    \
              RequestContext& ps_logging_internal_context = (request_context); \
          const int ps_logging_internal_verbose_level = (verbose_level))       \
  default:                                                                     \
    PS_LOG_INTERNAL_LOG_IF_IMPL(                                               \
        _INFO,                                                                 \
        ::privacy_sandbox::bidding_auction_servers::log::PS_VLOG_IS_ON(        \
            ps_logging_internal_verbose_level),                                \
        ps_logging_internal_context)                                           \
        .WithVerbosity(ps_logging_internal_verbose_level)
#else
#define PS_VLOG_INTERNAL(verbose_level, request_context)                       \
  switch (::privacy_sandbox::bidding_auction_servers::log::                    \
              RequestContext& ps_logging_internal_context = (request_context); \
          const int ps_logging_internal_verbose_level = (verbose_level))       \
  default:                                                                     \
    ABSL_LOG_IF(                                                               \
        INFO, ::privacy_sandbox::bidding_auction_servers::log::PS_VLOG_IS_ON(  \
                  ps_logging_internal_verbose_level) &&                        \
                  ps_logging_internal_context.is_consented())                  \
        .ToSinkOnly(ps_logging_internal_context.ConsentedSink())
#endif

#define PS_LOG_INTERNAL_LOG_IF_IMPL(severity, condition, request_context) \
  PS_LOG_INTERNAL_CONDITION(condition)                                    \
  PS_LOGGING_INTERNAL_LOG##severity(request_context).InternalStream()

// The `switch` ensures that this expansion is the begnning of a statement.
//
// The tenary evaluates to either
//   (void)0;
// or
//   ::absl::log_internal::Voidify() &&
//       PS_LOGGING_INTERNAL_LOG_INFO(request_context) << "log message";
//
// `Voidify()` is to avoid compiler wanring.
#define PS_LOG_INTERNAL_CONDITION(condition) \
  switch (0)                                 \
  case 0:                                    \
  default:                                   \
    !(condition) ? (void)0 : ::absl::log_internal::Voidify()&&

#define PS_LOGGING_INTERNAL_LOG_INFO(request_context)            \
  ::privacy_sandbox::bidding_auction_servers::log::PSLogMessage( \
      __FILE__, __LINE__, ::absl ::LogSeverity ::kInfo)          \
      .ToSinkAlsoIf(request_context.is_consented(),              \
                    request_context.ConsentedSink())             \
      .ToSinkAlsoIf(request_context.is_debug_response(),         \
                    request_context.DebugResponseSink())

#define PS_VLOG_CONTEXT_INTERNAL(verbose_level, request_context) \
  PS_VLOG_INTERNAL(verbose_level, request_context)               \
      << ps_logging_internal_context.ContextStr()

// Same as ABSL_LOG, just log with extra request_context.ContextStr()
#define PS_LOG(severity, request_context)                                      \
  switch (::privacy_sandbox::bidding_auction_servers::log::                    \
              RequestContext& ps_logging_internal_context = (request_context); \
          0)                                                                   \
  default:                                                                     \
    ABSL_LOG(severity) << ps_logging_internal_context.ContextStr()

#define PS_VLOG_NO_CONTEXT_INTERNAL(verbose_level)                        \
  PS_VLOG_INTERNAL(                                                       \
      verbose_level,                                                      \
      const_cast<                                                         \
          ::privacy_sandbox::bidding_auction_servers::log::NoOpContext&>( \
          ::privacy_sandbox::bidding_auction_servers::log::kNoOpContext))

// It can have 1 or 2 arguments. i.e.
// PS_VLOG(verbose_level)
//   Same as ABSL_VLOG(verbose_level), but only log in `non_prod`
// PS_VLOG(verbose_level, request_context)
//   Similar to ABSL_VLOG(verbose_level), but with extra functionality using
//   `request_context` (as `RequestContext&`)
#define PS_VLOG(...)                                 \
  GET_PS_VLOG(__VA_ARGS__, PS_VLOG_CONTEXT_INTERNAL, \
              PS_VLOG_NO_CONTEXT_INTERNAL)           \
  (__VA_ARGS__)
#define GET_PS_VLOG(_1, _2, NAME, ...) NAME

#endif  // SERVICES_COMMON_LOGGERS_REQUEST_CONTEXT_LOGGER_H_
