//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_COMMON_UTIL_STATUS_BUILDER_H_
#define SERVICES_COMMON_UTIL_STATUS_BUILDER_H_
#include <memory>
#include <string>
#include <utility>

#include "absl/base/log_severity.h"
#include "absl/status/status.h"
#include "absl/strings/internal/ostringstream.h"
#include "absl/time/time.h"
#include "services/common/util/source_location.h"

namespace privacy_sandbox::bidding_auction_servers {
// Creates a status based on an original_status, but enriched with additional
// information.  The builder implicitly converts to absl::Status and
// absl::StatusOr<T> allowing for it to be returned directly.
//
//   StatusBuilder builder(original);
//   util::AttachPayload(&builder, proto);
//   builder << "info about error";
//   return builder;
//
// It provides method chaining to simplify typical usage:
//
//   return StatusBuilder(original)
//       .Log(base_logging::WARNING) << "oh no!";
//
// In more detail:
// - When the original status is OK, all methods become no-ops and nothing will
//   be logged.
// - Messages streamed into the status builder are collected into a single
//   additional message string.
// - The original Status's message and the additional message are joined
//   together when the result status is built.
// - By default, the messages will be joined as if by `util::Annotate`, which
//   includes a convenience separator between the original message and the
//   additional one.  This behavior can be changed with the `SetAppend()` and
//   `SetPrepend()` methods of the builder.
// - By default, the result status is not logged.  The `Log` and
//   `EmitStackTrace` methods will cause the builder to log the result status
//   when it is built.
// - All side effects (like logging or constructing a stack trace) happen when
//   the builder is converted to a status.
class ABSL_MUST_USE_RESULT StatusBuilder {
 public:
  StatusBuilder();
  ~StatusBuilder() {}
  // Creates a `StatusBuilder` based on an original status.  If logging is
  // enabled, it will use `location` as the location from which the log message
  // occurs.  A typical user will not specify `location`, allowing it to default
  // to the current location.
  explicit StatusBuilder(const absl::Status& original_status,
                         SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
  explicit StatusBuilder(absl::Status&& original_status,
                         SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
  // Creates a `StatusBuilder` from a status code in
  // `util::CanonicalErrorSpace()`  If logging is enabled, it will use
  // `location` as the location from which the log message occurs.  A typical
  // user will not specify `location`, allowing it to default to the current
  // location.
  explicit StatusBuilder(absl::StatusCode code,
                         SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
  StatusBuilder(const StatusBuilder& sb);
  StatusBuilder& operator=(const StatusBuilder& sb);
  StatusBuilder(StatusBuilder&&) = default;
  StatusBuilder& operator=(StatusBuilder&&) = default;
  // Mutates the builder so that the final additional message is prepended to
  // the original error message in the status.  A convenience separator is not
  // placed between the messages.
  //
  // NOTE: Multiple calls to `SetPrepend` and `SetAppend` just adjust the
  // behavior of the final join of the original status with the extra message.
  //
  // Returns `*this` to allow method chaining.
  StatusBuilder& SetPrepend() &;
  ABSL_MUST_USE_RESULT StatusBuilder&& SetPrepend() &&;
  // Mutates the builder so that the final additional message is appended to the
  // original error message in the status.  A convenience separator is not
  // placed between the messages.
  //
  // NOTE: Multiple calls to `SetPrepend` and `SetAppend` just adjust the
  // behavior of the final join of the original status with the extra message.
  //
  // Returns `*this` to allow method chaining.
  StatusBuilder& SetAppend() &;
  ABSL_MUST_USE_RESULT StatusBuilder&& SetAppend() &&;
  // Mutates the builder to disable any logging that was set using any of the
  // logging functions below.  Returns `*this` to allow method chaining.
  StatusBuilder& SetNoLogging() &;
  ABSL_MUST_USE_RESULT StatusBuilder&& SetNoLogging() &&;
  // Mutates the builder so that the result status will be logged (without a
  // stack trace) when this builder is converted to a Status.  This overrides
  // the logging settings from earlier calls to any of the logging mutator
  // functions.  Returns `*this` to allow method chaining.
  StatusBuilder& Log(absl::LogSeverity level) &;
  ABSL_MUST_USE_RESULT StatusBuilder&& Log(absl::LogSeverity level) &&;
  StatusBuilder& LogError() & { return Log(absl::LogSeverity::kError); }
  ABSL_MUST_USE_RESULT StatusBuilder&& LogError() && {
    return std::move(LogError());
  }
  StatusBuilder& LogWarning() & { return Log(absl::LogSeverity::kWarning); }
  ABSL_MUST_USE_RESULT StatusBuilder&& LogWarning() && {
    return std::move(LogWarning());
  }
  StatusBuilder& LogInfo() & { return Log(absl::LogSeverity::kInfo); }
  ABSL_MUST_USE_RESULT StatusBuilder&& LogInfo() && {
    return std::move(LogInfo());
  }
  // Mutates the builder so that the result status will be logged every N
  // invocations (without a stack trace) when this builder is converted to a
  // Status.  This overrides the logging settings from earlier calls to any of
  // the logging mutator functions.  Returns `*this` to allow method chaining.
  StatusBuilder& LogEveryN(absl::LogSeverity level, int n) &;
  ABSL_MUST_USE_RESULT StatusBuilder&& LogEveryN(absl::LogSeverity level,
                                                 int n) &&;
  // Mutates the builder so that the result status will be logged once per
  // period (without a stack trace) when this builder is converted to a Status.
  // This overrides the logging settings from earlier calls to any of the
  // logging mutator functions.  Returns `*this` to allow method chaining.
  // If period is absl::ZeroDuration() or less, then this is equivalent to
  // calling the Log() method.
  StatusBuilder& LogEvery(absl::LogSeverity level, absl::Duration period) &;
  ABSL_MUST_USE_RESULT StatusBuilder&& LogEvery(absl::LogSeverity level,
                                                absl::Duration period) &&;
  // Mutates the builder so that the result status will be VLOGged (without a
  // stack trace) when this builder is converted to a Status.  `verbose_level`
  // indicates the verbosity level that would be passed to VLOG().  This
  // overrides the logging settings from earlier calls to any of the logging
  // mutator functions.  Returns `*this` to allow method chaining.
  StatusBuilder& VLog(int verbose_level) &;
  ABSL_MUST_USE_RESULT StatusBuilder&& VLog(int verbose_level) &&;
  // Mutates the builder so that a stack trace will be logged if the status is
  // logged. One of the logging setters above should be called as well. If
  // logging is not yet enabled this behaves as if LogInfo().EmitStackTrace()
  // was called. Returns `*this` to allow method chaining.
  StatusBuilder& EmitStackTrace() &;
  ABSL_MUST_USE_RESULT StatusBuilder&& EmitStackTrace() &&;
  // Appends to the extra message that will be added to the original status.  By
  // default, the extra message is added to the original message as if by
  // `util::Annotate`, which includes a convenience separator between the
  // original message and the enriched one.
  template <typename T>
  StatusBuilder& operator<<(const T& value) &;
  template <typename T>
  ABSL_MUST_USE_RESULT StatusBuilder&& operator<<(const T& value) &&;
  // Sets the status code for the status that will be returned by this
  // StatusBuilder. Returns `*this` to allow method chaining.
  StatusBuilder& SetCode(absl::StatusCode code) &;
  ABSL_MUST_USE_RESULT StatusBuilder&& SetCode(absl::StatusCode code) &&;
  ///////////////////////////////// Adaptors /////////////////////////////////
  //
  // A StatusBuilder `adaptor` is a functor which can be included in a builder
  // method chain. There are two common variants:
  //
  // 1. `Pure policy` adaptors modify the StatusBuilder and return the modified
  //    object, which can then be chained with further adaptors or mutations.
  //
  // 2. `Terminal` adaptors consume the builder's Status and return some
  //    other type of object. Alternatively, the consumed Status may be used
  //    for side effects, e.g. by passing it to a side channel. A terminal
  //    adaptor cannot be chained.
  //
  // Careful: The conversion of StatusBuilder to Status has side effects!
  // Adaptors must ensure that this conversion happens at most once in the
  // builder chain. The easiest way to do this is to determine the adaptor type
  // and follow the corresponding guidelines:
  //
  // Pure policy adaptors should:
  // (a) Take a StatusBuilder as input parameter.
  // (b) NEVER convert the StatusBuilder to Status:
  //     - Never assign the builder to a Status variable.
  //     - Never pass the builder to a function whose parameter type is Status,
  //       including by reference (e.g. const absl::Status&).
  //     - Never pass the builder to any function which might convert the
  //       builder to Status (i.e. this restriction is viral).
  // (c) Return a StatusBuilder (usually the input parameter).
  //
  // Terminal adaptors should:
  // (a) Take a Status as input parameter (not a StatusBuilder!).
  // (b) Return a type matching the enclosing function. (This can be `void`.)
  //
  // Adaptors do not necessarily fit into one of these categories. However, any
  // which satisfies the conversion rule can always be decomposed into a pure
  // adaptor chained into a terminal adaptor. (This is recommended.)
  //
  // Examples
  //
  // Pure adaptors allow teams to configure team-specific error handling
  // policies.  For example:
  //
  //   StatusBuilder TeamPolicy(StatusBuilder builder) {
  //     util::AttachPayload(&builder, ...);
  //     return std::move(builder).Log(base_logging::WARNING);
  //   }
  //
  //   PS_RETURN_IF_ERROR(foo()).With(TeamPolicy);
  //
  // Because pure policy adaptors return the modified StatusBuilder, they
  // can be chained with further adaptors, e.g.:
  //
  //   PS_RETURN_IF_ERROR(foo()).With(TeamPolicy).With(OtherTeamPolicy);
  //
  // Terminal adaptors are often used for type conversion. This allows
  // PS_RETURN_IF_ERROR to be used in functions which do not return Status. For
  // example, a function might want to return some default value on error:
  //
  //   int GetSysCounter() {
  //     int value;
  //     PS_RETURN_IF_ERROR(ReadCounterFile(filename, &value))
  //         .LogInfo()
  //         .With([](const absl::Status& unused) { return 0; });
  //     return value;
  //   }
  //
  // For the simple case of returning a constant (e.g. zero, false, nullptr) on
  // error, consider `status_macros::Return` or `status_macros::ReturnVoid`:
  //
  //   #include "util/task/contrib/status_macros/return.h"
  //
  //   bool DoMyThing() {
  //     PS_RETURN_IF_ERROR(foo()).LogWarning().With(status_macros::Return(false));
  //     ...
  //   }
  //
  // A terminal adaptor may instead (or additionally) be used to create side
  // effects that are not supported natively by `StatusBuilder`, such as
  // returning the Status through a side channel. For example,
  // `util::TaskReturn` returns the Status through the `util::Task` that it was
  // initialized with. This adaptor then returns `void`, to match the typical
  // return type of functions that maintain state through `util::Task`:
  //
  //   class TaskReturn {
  //    public:
  //     explicit TaskReturn(Task* t) : task_(t) {}
  //     void operator()(const absl::Status& status) const {
  //     task_->Return(status); }
  //     // ...
  //   };
  //
  //   void Read(absl::string_view name, util::Task* task) {
  //     int64 id;
  //     PS_RETURN_IF_ERROR(GetIdForName(name, &id)).With(TaskReturn(task));
  //     PS_RETURN_IF_ERROR(ReadForId(id)).With(TaskReturn(task));
  //     task->Return();
  //   }
  // Calls `adaptor` on this status builder to apply policies, type conversions,
  // and/or side effects on the StatusBuilder. Returns the value returned by
  // `adaptor`, which may be any type including `void`. See comments above.
  template <typename Adaptor>
  auto With(
      Adaptor&& adaptor) & -> decltype(std::forward<Adaptor>(adaptor)(*this)) {
    return std::forward<Adaptor>(adaptor)(*this);
  }
  template <typename Adaptor>
  ABSL_MUST_USE_RESULT auto
  With(Adaptor&& adaptor) && -> decltype(std::forward<Adaptor>(adaptor)(
      std::move(*this))) {
    return std::forward<Adaptor>(adaptor)(std::move(*this));
  }
  // Returns true if the Status created by this builder will be ok().
  ABSL_MUST_USE_RESULT bool ok() const;
  // Returns the (canonical) error code for the Status created by this builder.
  ABSL_MUST_USE_RESULT absl::StatusCode code() const;
  // Implicit conversion to Status.
  //
  // Careful: this operator has side effects, so it should be called at
  // most once. In particular, do NOT use this conversion operator to inspect
  // the status from an adapter object passed into With().
  operator absl::Status() const&;  // NOLINT: Builder converts implicitly.
  operator absl::Status() &&;      // NOLINT: Builder converts implicitly.
  // Returns the source location used to create this builder.
  ABSL_MUST_USE_RESULT SourceLocation source_location() const;

 private:
  // Specifies how to join the error message in the original status and any
  // additional message that has been streamed into the builder.
  enum class MessageJoinStyle {
    kAnnotate,
    kAppend,
    kPrepend,
  };
  // Creates a new status based on an old one by joining the message from the
  // original to an additional message.
  absl::Status JoinMessageToStatus(absl::Status s, absl::string_view msg,
                                   MessageJoinStyle style);
  // Creates a Status from this builder and logs it if the builder has been
  // configured to log itself.
  absl::Status CreateStatusAndConditionallyLog() &&;
  // Conditionally logs if the builder has been configured to log.  This method
  // is split from the above to isolate the portability issues around logging
  // into a single place.
  void ConditionallyLog(const absl::Status& status) const;
  // Infrequently set builder options, instantiated lazily. This reduces
  // average construction/destruction time (e.g. the `stream` is fairly
  // expensive). Stacks can also be blown if StatusBuilder grows too large.
  // This is primarily an issue for debug builds, which do not necessarily
  // re-use stack space within a function across the sub-scopes used by
  // status macros.
  struct Rep {
    explicit Rep(const absl::Status& s);
    explicit Rep(absl::Status&& s);
    Rep(const Rep& r);
    ~Rep();
    // The status that the result will be based on.  Can be modified by
    // util::AttachPayload().
    absl::Status status;
    enum class LoggingMode {
      kDisabled,
      kLog,
      kVLog,
      kLogEveryN,
    };
    LoggingMode logging_mode = LoggingMode::kDisabled;
    // The severity level at which the Status should be logged. Note that
    // `logging_mode == LoggingMode::kVLog` always logs at severity INFO.
    absl::LogSeverity log_severity;
    // The level at which the Status should be VLOGged.
    // Only used when `logging_mode == LoggingMode::kVLog`.
    int verbose_level;
    // Only log every N invocations.
    // Only used when `logging_mode == LoggingMode::kLogEveryN`.
    int n;
    // Only log once per period.
    // Only used when `logging_mode == LoggingMode::kLogEveryPeriod`.
    absl::Duration period;
    // Gathers additional messages added with `<<` for use in the final status.
    std::string stream_message;
    absl::strings_internal::OStringStream stream{&stream_message};
    // Whether to log stack trace.  Only used when `logging_mode !=
    // LoggingMode::kDisabled`.
    bool should_log_stack_trace = false;
    // Specifies how to join the message in `status` and `stream`.
    MessageJoinStyle message_join_style = MessageJoinStyle::kAnnotate;
  };
  static std::unique_ptr<Rep> InitRep(const absl::Status& s) {
    if (s.ok()) {
      return nullptr;
    } else {
      return std::make_unique<Rep>(s);
    }
  }
  static std::unique_ptr<Rep> InitRep(absl::Status&& s) {
    if (s.ok()) {
      return nullptr;
    } else {
      return std::make_unique<Rep>(std::move(s));
    }
  }
  const absl::Status& RepStatusOrOk() const {
    static absl::Status* ok = new absl::Status;
    return rep_ == nullptr ? *ok : rep_->status;
  }
  // The location to record if this status is logged.
  SourceLocation loc_;
  // nullptr if the result status will be OK.  Extra fields moved to the heap to
  // minimize stack space.
  std::unique_ptr<Rep> rep_;
};
// Implicitly converts `builder` to `Status` and write it to `os`.
std::ostream& operator<<(std::ostream& os, const StatusBuilder& builder);
std::ostream& operator<<(std::ostream& os, StatusBuilder&& builder);
// Each of the functions below creates StatusBuilder with a canonical error.
// The error code of the StatusBuilder matches the name of the function.
StatusBuilder AbortedErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder AlreadyExistsErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder CancelledErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder DataLossErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder DeadlineExceededErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder FailedPreconditionErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder InternalErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder InvalidArgumentErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder NotFoundErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder OutOfRangeErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder PermissionDeniedErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder UnauthenticatedErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder ResourceExhaustedErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder UnavailableErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder UnimplementedErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
StatusBuilder UnknownErrorBuilder(
    SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);
// StatusBuilder policy to append an extra message to the original status.
//
// This is most useful with adaptors such as util::TaskReturn that otherwise
// would prevent use of operator<<.  For example:
//
//   AIS_PS_RETURN_IF_ERROR(foo(val))
//       .With(util::ExtraMessage("when calling foo()"))
//       .With(util::TaskReturn(task));
//
// or
//
//   AIS_PS_RETURN_IF_ERROR(foo(val))
//       .With(util::ExtraMessage() << "val: " << val)
//       .With(util::TaskReturn(task));
//
// Note in the above example, the PS_RETURN_IF_ERROR macro ensures the
// ExtraMessage expression is evaluated only in the error case, so efficiency of
// constructing the message is not a concern in the success case.
class ExtraMessage {
 public:
  ExtraMessage() : ExtraMessage(std::string()) {}
  explicit ExtraMessage(std::string msg)
      : msg_(std::move(msg)), stream_(&msg_) {}
  // Appends to the extra message that will be added to the original status.  By
  // default, the extra message is added to the original message as if by
  // `util::Annotate`, which includes a convenience separator between the
  // original message and the enriched one.
  template <typename T>
  ExtraMessage& operator<<(const T& value) {
    stream_ << value;
    return *this;
  }
  // Appends to the extra message that will be added to the original status.  By
  // default, the extra message is added to the original message as if by
  // `util::Annotate`, which includes a convenience separator between the
  // original message and the enriched one.
  StatusBuilder operator()(StatusBuilder builder) const {
    builder << msg_;
    return builder;
  }

 private:
  std::string msg_;
  absl::strings_internal::OStringStream stream_;
};
// Implementation details follow; clients should ignore.
inline StatusBuilder::StatusBuilder(const absl::Status& original_status,
                                    SourceLocation location)
    : loc_(location), rep_(InitRep(original_status)) {}
inline StatusBuilder::StatusBuilder(absl::Status&& original_status,
                                    SourceLocation location)
    : loc_(location), rep_(InitRep(std::move(original_status))) {}
inline StatusBuilder::StatusBuilder(const StatusBuilder& sb) : loc_(sb.loc_) {
  if (sb.rep_ != nullptr) {
    rep_ = std::make_unique<Rep>(*sb.rep_);
  }
}
inline StatusBuilder::StatusBuilder(absl::StatusCode code,
                                    SourceLocation location)
    : loc_(location), rep_(InitRep(absl::Status(code, ""))) {}
inline StatusBuilder& StatusBuilder::operator=(const StatusBuilder& sb) {
  loc_ = sb.loc_;
  if (sb.rep_ != nullptr) {
    rep_ = std::make_unique<Rep>(*sb.rep_);
  } else {
    rep_ = nullptr;
  }
  return *this;
}
inline StatusBuilder& StatusBuilder::SetPrepend() & {
  if (rep_ == nullptr) return *this;
  rep_->message_join_style = MessageJoinStyle::kPrepend;
  return *this;
}
inline StatusBuilder&& StatusBuilder::SetPrepend() && {
  return std::move(SetPrepend());
}
inline StatusBuilder& StatusBuilder::SetAppend() & {
  if (rep_ == nullptr) return *this;
  rep_->message_join_style = MessageJoinStyle::kAppend;
  return *this;
}
inline StatusBuilder&& StatusBuilder::SetAppend() && {
  return std::move(SetAppend());
}
inline StatusBuilder& StatusBuilder::SetNoLogging() & {
  if (rep_ != nullptr) {
    rep_->logging_mode = Rep::LoggingMode::kDisabled;
    rep_->should_log_stack_trace = false;
  }
  return *this;
}
inline StatusBuilder&& StatusBuilder::SetNoLogging() && {
  return std::move(SetNoLogging());
}
inline StatusBuilder& StatusBuilder::Log(absl::LogSeverity level) & {
  if (rep_ == nullptr) return *this;
  rep_->logging_mode = Rep::LoggingMode::kLog;
  rep_->log_severity = level;
  return *this;
}
inline StatusBuilder&& StatusBuilder::Log(absl::LogSeverity level) && {
  return std::move(Log(level));
}
inline StatusBuilder& StatusBuilder::LogEveryN(absl::LogSeverity level,
                                               int n) & {
  if (rep_ == nullptr) return *this;
  if (n < 1) return Log(level);
  rep_->logging_mode = Rep::LoggingMode::kLogEveryN;
  rep_->log_severity = level;
  rep_->n = n;
  return *this;
}
inline StatusBuilder&& StatusBuilder::LogEveryN(absl::LogSeverity level,
                                                int n) && {
  return std::move(LogEveryN(level, n));
}
// ToDo(b/261739087): enable with absl vlog support
// inline StatusBuilder& StatusBuilder::VLog(int verbose_level) & {
//   if (rep_ == nullptr) return *this;
//   rep_->logging_mode = Rep::LoggingMode::kVLog;
//   rep_->verbose_level = verbose_level;
//   return *this;
// }
// inline StatusBuilder&& StatusBuilder::VLog(int verbose_level) && {
//   return std::move(VLog(verbose_level));
// }
inline StatusBuilder& StatusBuilder::EmitStackTrace() & {
  if (rep_ == nullptr) return *this;
  if (rep_->logging_mode == Rep::LoggingMode::kDisabled) {
    // Default to INFO logging, otherwise nothing would be emitted.
    rep_->logging_mode = Rep::LoggingMode::kLog;
    rep_->log_severity = absl::LogSeverity::kInfo;
  }
  rep_->should_log_stack_trace = true;
  return *this;
}
inline StatusBuilder&& StatusBuilder::EmitStackTrace() && {
  return std::move(EmitStackTrace());
}
template <typename T>
StatusBuilder& StatusBuilder::operator<<(const T& value) & {
  if (rep_ == nullptr) return *this;
  rep_->stream << value;
  return *this;
}
template <typename T>
StatusBuilder&& StatusBuilder::operator<<(const T& value) && {
  return std::move(operator<<(value));
}
inline bool StatusBuilder::ok() const {
  return rep_ == nullptr ? true : rep_->status.ok();
}
inline absl::StatusCode StatusBuilder::code() const {
  return rep_ == nullptr ? absl::StatusCode::kOk : rep_->status.code();
}
inline StatusBuilder::operator absl::Status() const& {
  if (rep_ == nullptr) return absl::Status();
  return StatusBuilder(*this).CreateStatusAndConditionallyLog();
}
inline StatusBuilder::operator absl::Status() && {
  if (rep_ == nullptr) return absl::Status();
  return std::move(*this).CreateStatusAndConditionallyLog();
}
inline SourceLocation StatusBuilder::source_location() const { return loc_; }
}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_COMMON_UTIL_STATUS_BUILDER_H_
