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

#include "services/common/loggers/request_context_impl.h"

#include <vector>

#include "absl/strings/str_join.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers::log {

std::string ContextImpl::FormatContext(const ContextMap& context_map) {
  if (context_map.empty()) {
    return "";
  }
  std::vector<std::string> pairs;
  for (const auto& [key, val] : context_map) {
    if (!val.empty()) {
      pairs.emplace_back(absl::StrCat(key, ": ", val));
    }
  }
  if (pairs.empty()) {
    return "";
  }
  return absl::StrCat(" (", absl::StrJoin(std::move(pairs), ", "), ") ");
}

}  // namespace privacy_sandbox::bidding_auction_servers::log
