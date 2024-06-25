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

#ifndef SERVICES_COMMON_UTIL_ERROR_ACCUMULATOR_H_
#define SERVICES_COMMON_UTIL_ERROR_ACCUMULATOR_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/loggers/source_location_context.h"
#include "services/common/util/error_reporter.h"
#include "src/util/status_macro/source_location.h"

namespace privacy_sandbox::bidding_auction_servers {

// A helper module to streamline error reporting.
//
// This utility class provides a way for services to report errors and
// categorize them and makes it easier to extract these errors and filter them
// so that these errors can be propagated to the right destination.
class ErrorAccumulator : public ErrorReporter {
 public:
  using ErrorMap = std::map<ErrorCode, std::set<std::string>>;

  ErrorAccumulator() = default;
  explicit ErrorAccumulator(RequestLogContext* log_context);
  virtual ~ErrorAccumulator() = default;

  // ErrorAccumulator is neither copyable nor movable.
  ErrorAccumulator(const ErrorAccumulator&) = delete;
  ErrorAccumulator& operator=(const ErrorAccumulator&) = delete;

  void ReportError(
      log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
      absl::string_view msg,
      ErrorCode error_code = ErrorCode::CLIENT_SIDE) override;

  void ReportError(const server_common::SourceLocation& location,
                   ErrorVisibility error_visibility, absl::string_view msg,
                   ErrorCode error_code = ErrorCode::CLIENT_SIDE) override;

  // Indicates whether or not this object knows about any errors.
  bool HasErrors() const override;

  // Gets the list of errors known to this object.
  const ErrorMap& GetErrors(ErrorVisibility error_visibility) const;

  // Gets a string of all errors concatenated by visibility.
  std::string GetAccumulatedErrorString(ErrorVisibility error_visibility);

 private:
  // Mapping from error visibility => { Error Code => List of Errors }.
  absl::flat_hash_map<ErrorVisibility, ErrorMap> dst_error_map_;
  const ErrorMap empty_error_map_ = {};

  // Optional log_context to be used for error reporting.
  RequestLogContext* log_context_ = nullptr;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_ERROR_ACCUMULATOR_H_
