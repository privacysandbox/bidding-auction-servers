// Copyright 2024 Google LLC
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

#include "utils/cpu.h"

#include <errno.h>
#include <sched.h>
#include <unistd.h>

#include <vector>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

absl::Status SetCpuAffinity(const std::vector<int>& cpus) {
  if (cpus.size() == 0) {
    return absl::InvalidArgumentError("cpus should have at least one element.");
  }
  cpu_set_t allowed_cpus;
  if (sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus) != 0) {
    // Failed to get CPU affinity of the current process.
    return absl::ErrnoToStatus(
        errno, absl::StrCat("sched_getaffinity() failed: ", errno));
  }

  const int num_virtual_cpus = CPU_COUNT(&allowed_cpus);
  CPU_ZERO(&allowed_cpus);
  ABSL_LOG(INFO) << "Num of the available CPUs: " << num_virtual_cpus;

  for (int cpu : cpus) {
    if (cpu >= num_virtual_cpus) {
      ABSL_LOG(WARNING) << "CPU (" << cpu << ") might not be available.";
    }
    CPU_SET(cpu, &allowed_cpus);
  }

  if (sched_setaffinity(0, sizeof(allowed_cpus), &allowed_cpus) != 0) {
    return absl::ErrnoToStatus(
        errno, absl::StrCat("sched_setaffinity() failed: ", errno));
  }
  const int num_pinned_cpus = CPU_COUNT(&allowed_cpus);
  ABSL_LOG(INFO) << "Num of the pinned CPUs: " << num_pinned_cpus;
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
