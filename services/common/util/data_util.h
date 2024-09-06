/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_UTIL_DATA_UTIL_H_
#define SERVICES_COMMON_UTIL_DATA_UTIL_H_

#include <cstddef>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Finds the provided string needle index in the haystack array.
template <std::size_t Size>
int FindItemIndex(const std::array<absl::string_view, Size>& haystack,
                  absl::string_view needle) {
  auto it = std::find(haystack.begin(), haystack.end(), needle);
  if (it == haystack.end()) {
    return -1;
  }

  return std::distance(haystack.begin(), it);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_DATA_UTIL_H_
