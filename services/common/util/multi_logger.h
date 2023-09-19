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

#ifndef SERVICES_COMMON_UTIL_MULTI_LOGGER_H_
#define SERVICES_COMMON_UTIL_MULTI_LOGGER_H_

#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "glog/vlog_is_on.h"
#include "services/common/util/consented_debugging_logger.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/source_location.h"

namespace privacy_sandbox::bidding_auction_servers {

// Utility class to log messages.
// It exposes vlog() method to control both ContextLogger and
// ConsentedDebuggingLogger.
class MultiLogger {
 public:
  using ContextMap = ContextLogger::ContextMap;

  MultiLogger(const ContextMap& context_map,
              absl::string_view server_debug_token)
      : context_logger_(context_map),
        consented_logger_(context_map, server_debug_token) {}

  virtual ~MultiLogger() = default;

  ContextLogger& GetContextLogger() { return context_logger_; }
  ConsentedDebuggingLogger& GetConsentedLogger() { return consented_logger_; }

  // vlog writes logs via the underlying loggers (i.e., ContextLogger and
  // ConsentedDebuggingLogger).
  template <class... T>
  void vlog(ParamWithSourceLoc<int> verbosity_with_source_loc,
            T&&... msg) const {
    const SourceLocation& location = verbosity_with_source_loc.location;
    int verbosity = verbosity_with_source_loc.mandatory_param;

    context_logger_.vlog(location, verbosity, std::forward<T>(msg)...);
    if (consented_logger_.IsConsented()) {
      consented_logger_.vlog(verbosity_with_source_loc,
                             absl::StrCat(std::forward<T>(msg)...));
    }
  }

  bool ShouldLog(int verbose_level) {
    return VLOG_IS_ON(verbose_level) || consented_logger_.IsConsented();
  }

  void SetContext(const ContextMap& context_map) {
    context_logger_.Configure(context_map);
    consented_logger_.SetContext(context_map);
  }

 private:
  ContextLogger context_logger_;
  ConsentedDebuggingLogger consented_logger_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_MULTI_LOGGER_H_
