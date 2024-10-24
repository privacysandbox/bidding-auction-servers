/*
 * Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_TCMALLOC_UTILS_H_
#define SERVICES_COMMON_UTIL_TCMALLOC_UTILS_H_

#include <cstdint>
#include <optional>

#include "src/logger/request_context_logger.h"
#include "tcmalloc/malloc_extension.h"

namespace privacy_sandbox::bidding_auction_servers {

inline void MaySetBackgroundReleaseRate(
    std::optional<int64_t> release_rate_bytes_per_second) {
  if (!release_rate_bytes_per_second) {
    PS_VLOG(4) << "No release_rate_bytes_per_second for TCMalloc "
                  "specified, will use default";
    return;
  }

  PS_VLOG(4) << "Using release_rate_bytes_per_second for TCMalloc: "
             << *release_rate_bytes_per_second;
  tcmalloc::MallocExtension::SetBackgroundReleaseRate(
      static_cast<tcmalloc::MallocExtension::BytesPerSecond>(
          *release_rate_bytes_per_second));
}

inline void MaySetMaxTotalThreadCacheBytes(
    std::optional<int64_t> max_total_thread_cache_bytes) {
  if (!max_total_thread_cache_bytes) {
    PS_VLOG(4) << "No max_total_thread_cache_bytes for TCMalloc specified, "
                  "will use default";
    return;
  }

  PS_VLOG(4) << "Using max_total_thread_cache_bytes for TCMalloc: "
             << *max_total_thread_cache_bytes;
  tcmalloc::MallocExtension::SetMaxTotalThreadCacheBytes(
      *max_total_thread_cache_bytes);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_TCMALLOC_UTILS_H_
