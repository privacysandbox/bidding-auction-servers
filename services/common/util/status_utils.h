/*
 * Copyright 2025 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_STATUS_UTILS_H_
#define SERVICES_COMMON_UTIL_STATUS_UTILS_H_

#include <algorithm>

#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Returns the status of the first not OK Status. Returns OK if all are OK.
inline absl::Status FirstNotOkStatus(absl::Span<absl::Status> statuses) {
  auto it = std::find_if_not(statuses.begin(), statuses.end(),
                             std::mem_fn(&absl::Status::ok));

  return (it != statuses.end()) ? *it : absl::OkStatus();
}

// Returns the status of the first not OK StatusOr. Returns OK if all are OK.
template <typename... T>
absl::Status FirstNotOkStatusOr(const absl::StatusOr<T>&... statuses) {
  absl::Status result = absl::OkStatus();
  auto check_status = [&](const auto& status_or) {
    if (!status_or.ok()) {
      result = status_or.status();
      return false;
    }

    return true;
  };

  (void)(check_status(statuses) && ...);
  return result;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_STATUS_UTILS_H_
