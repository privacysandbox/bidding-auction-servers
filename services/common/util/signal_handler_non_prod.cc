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

#include <utility>

#include "services/common/util/file_util.h"
#include "services/common/util/signal_handler.h"
#include "tcmalloc/malloc_extension.h"
#include "tcmalloc/profile_marshaler.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr int kDefaultHeapDumpSignal = 27;
inline constexpr absl::string_view kDefaultHeapProfilePath = "service.hprof";

void HeapProfileHandler(int sig) {
  static bool heap_profiler_running = false;
  static tcmalloc::MallocExtension::AllocationProfilingToken profiling_token;
  if (!heap_profiler_running) {
    fprintf(stdout, "Starting heap profiling\n");
    profiling_token = tcmalloc::MallocExtension::StartAllocationProfiling();
    heap_profiler_running = true;
  } else {
    fprintf(stdout, "Stopping heap profiling\n");
    auto profile = std::move(profiling_token).Stop();
    heap_profiler_running = false;

    auto pprof_data = tcmalloc::Marshal(profile);
    if (!pprof_data.ok()) {
      fprintf(stderr, "Failed to marshal heap profile:\n%s\n",
              pprof_data.status().message().data());
      return;
    }

    static int i = 0;
    static char* profile_name = getenv("HEAPPROFILE");
    if (profile_name) {
      if (!WriteToFile(absl::StrCat(profile_name, "_", i++), *pprof_data)
               .ok()) {
        fprintf(stderr, "Failed to write converted profile\n");
      }
    } else {
      if (!WriteToFile(absl::StrCat(kDefaultHeapProfilePath, "_", i++),
                       *pprof_data)
               .ok()) {
        fprintf(stderr, "Failed to write converted profile\n");
      }
    }
  }
}

}  // namespace

void RegisterCommonSignalHandlers() {
  RegisterCommonSignalHandlersHelper();
  char* heap_profiler_sig_str = getenv("HEAPPROFILESIGNAL");
  if (heap_profiler_sig_str) {
    long int heap_profiler_sig = strtol(heap_profiler_sig_str, NULL, 10);
    signal(heap_profiler_sig, HeapProfileHandler);
  } else {
    signal(kDefaultHeapDumpSignal, HeapProfileHandler);
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
