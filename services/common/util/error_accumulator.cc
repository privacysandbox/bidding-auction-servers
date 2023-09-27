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

#include "services/common/util/error_accumulator.h"

#include <string>

namespace privacy_sandbox::bidding_auction_servers {

ErrorAccumulator::ErrorAccumulator(ContextLogger* logger) : logger_(logger) {}

void ErrorAccumulator::ReportError(
    ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    absl::string_view msg, ErrorCode error_code) {
  ReportError(error_visibility_with_loc.location,
              error_visibility_with_loc.mandatory_param, msg, error_code);
}

void ErrorAccumulator::ReportError(
    const server_common::SourceLocation& location,
    ErrorVisibility error_visibility, absl::string_view msg,
    ErrorCode error_code) {
  dst_error_map_[error_visibility][error_code].emplace(msg);
  if (logger_) {
    logger_->vlog(location, 2, msg);
  }
}

const ErrorAccumulator::ErrorMap& ErrorAccumulator::GetErrors(
    ErrorVisibility error_visibility) const {
  auto it = dst_error_map_.find(error_visibility);
  if (it == dst_error_map_.end()) {
    return empty_error_map_;
  }

  return it->second;
}

bool ErrorAccumulator::HasErrors() const { return !dst_error_map_.empty(); }

}  // namespace privacy_sandbox::bidding_auction_servers
