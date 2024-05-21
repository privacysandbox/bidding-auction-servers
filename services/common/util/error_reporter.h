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

#ifndef SERVICES_COMMON_UTIL_ERROR_REPORTER_H_
#define SERVICES_COMMON_UTIL_ERROR_REPORTER_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/loggers/source_location_context.h"
#include "services/common/util/error_categories.h"
#include "src/util/status_macro/source_location.h"

namespace privacy_sandbox::bidding_auction_servers {

// Interface definition for a class to which errors can be reported and queried.
class ErrorReporter {
 public:
  ErrorReporter() = default;
  virtual ~ErrorReporter() = default;

  // Informs the object about details of an error. Callers can call this method
  // directly without explicitly creating the log::ParamWithSourceLoc object (by
  // just passing in the error_visibility instead) and the source location
  // context will be captured by this method automatically.
  virtual void ReportError(
      log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
      absl::string_view msg, ErrorCode error_code = ErrorCode::CLIENT_SIDE) = 0;

  // Informs the object about details of an error. This variant is helpful
  // for cases when we have wrappers around the error reporter so that we can
  // preserve the top most source location where the error reporting call was
  // invoked.
  virtual void ReportError(const server_common::SourceLocation& location,
                           ErrorVisibility error_visibility,
                           absl::string_view msg,
                           ErrorCode error_code = ErrorCode::CLIENT_SIDE) = 0;

  // Indicates whether any errors were reported to this object previously.
  virtual bool HasErrors() const = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_ERROR_REPORTER_H_
