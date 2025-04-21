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

#ifndef SERVICES_COMMON_UTIL_MAP_UTILS_H_
#define SERVICES_COMMON_UTIL_MAP_UTILS_H_

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Returns the keys of the map.
template <typename T, typename U>
absl::flat_hash_set<T> KeySet(absl::flat_hash_map<T, U> input) {
  absl::flat_hash_set<T> keys;
  for (const auto& entry : input) {
    keys.insert(entry.first);
  }

  return keys;
}

// Specialization that returns absl::string_view for maps with std::string keys.
template <typename T>
absl::flat_hash_set<absl::string_view> KeySet(
    absl::flat_hash_map<std::string, T> input) {
  absl::flat_hash_set<absl::string_view> keys;
  for (const auto& entry : input) {
    keys.insert(absl::string_view(entry.first));
  }

  return keys;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_MAP_UTILS_H_
