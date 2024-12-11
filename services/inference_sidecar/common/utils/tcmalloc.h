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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_TCMALLOC_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_TCMALLOC_H_

#include <cstdint>

#include "tcmalloc/malloc_extension.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

void SetTcMallocParams(int64_t release_bytes_per_sec,
                       int64_t max_total_thread_cache_bytes,
                       int32_t max_per_cpu_cache_bytes) {
  if (release_bytes_per_sec != 0) {
    tcmalloc::MallocExtension::SetBackgroundReleaseRate(
        static_cast<tcmalloc::MallocExtension::BytesPerSecond>(
            release_bytes_per_sec));
  }
  if (max_total_thread_cache_bytes != 0) {
    tcmalloc::MallocExtension::SetMaxTotalThreadCacheBytes(
        max_total_thread_cache_bytes);
  }
  if (max_per_cpu_cache_bytes != 0) {
    tcmalloc::MallocExtension::SetMaxPerCpuCacheSize(max_per_cpu_cache_bytes);
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_TCMALLOC_H_
