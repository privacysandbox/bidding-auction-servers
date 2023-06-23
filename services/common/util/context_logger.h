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

#ifndef SERVICES_COMMON_UTIL_CONTEXT_LOGGER_H_
#define SERVICES_COMMON_UTIL_CONTEXT_LOGGER_H_

#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "glog/vlog_is_on.h"
#include "services/common/util/source_location.h"

namespace privacy_sandbox::bidding_auction_servers {

template <class T>
struct ParamWithSourceLoc {
  T mandatory_param;
  SourceLocation location;
  template <class U>
  ParamWithSourceLoc(U param, SourceLocation loc_in PS_LOC_CURRENT_DEFAULT_ARG)
      : mandatory_param(std::forward<U>(param)), location(loc_in) {}
};

// Utility class that caches the passed in context as a string upon construction
// and logs the cached context with the provided log message when needed.
class ContextLogger {
 public:
  using ContextMap = std::map<std::string, std::string>;

  ContextLogger() = default;
  explicit ContextLogger(const ContextMap& context_map);
  virtual ~ContextLogger() = default;

  template <class... T>
  void vlog(ParamWithSourceLoc<int> verbosity_with_source_loc,
            T&&... msg) const {
    const SourceLocation& location = verbosity_with_source_loc.location;
    int verbosity = verbosity_with_source_loc.mandatory_param;
    vlog(location, verbosity, std::forward<T>(msg)...);
  }

  // Sometimes we will have wrappers around the logger and vlog.
  template <class... T>
  void vlog(const SourceLocation& location, int verbosity, T&&... msg) const {
    if (VLOG_IS_ON(verbosity)) {
      ((google::LogMessage(location.file_name(), location.line(),
                           google::GLOG_INFO)
            .stream()
        << context_)
       << ... << std::forward<T>(msg));
    }
  }

  template <class... T>
  void info(ParamWithSourceLoc<absl::string_view> first_msg_with_loc,
            T&&... msg) const {
    LogWithSeverity(google::GLOG_INFO, first_msg_with_loc,
                    std::forward<T>(msg)...);
  }

  template <class... T>
  void warn(ParamWithSourceLoc<absl::string_view> first_msg_with_loc,
            T&&... msg) const {
    LogWithSeverity(google::GLOG_WARNING, first_msg_with_loc,
                    std::forward<T>(msg)...);
  }

  template <class... T>
  void error(ParamWithSourceLoc<absl::string_view> first_msg_with_loc,
             T&&... msg) const {
    LogWithSeverity(google::GLOG_ERROR, first_msg_with_loc,
                    std::forward<T>(msg)...);
  }

  void Configure(const ContextMap& context_map);

 private:
  template <class... T>
  void LogWithSeverity(
      google::LogSeverity log_severity,
      const ParamWithSourceLoc<absl::string_view>& first_msg_with_loc,
      T&&... msg) const {
    const SourceLocation& location = first_msg_with_loc.location;
    ((google::LogMessage(location.file_name(), location.line(), log_severity)
          .stream()
      << context_ << first_msg_with_loc.mandatory_param)
     << ... << std::forward<T>(msg));
  }

  std::string context_;
};

// Utility method to format the context provided as key/value pair into a
// string. Function excludes any empty values from the output string.
std::string FormatContext(const ContextLogger::ContextMap& context_map);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_CONTEXT_LOGGER_H_
