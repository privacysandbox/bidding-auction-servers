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
#ifndef SERVICES_COMMON_UTIL_READ_SYSTEM_H_
#define SERVICES_COMMON_UTIL_READ_SYSTEM_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace privacy_sandbox::server_common {

class SystemMetrics {
 public:
  // Sets the PID for inference sidecar
  static void SetInferencePid(pid_t pid);

  // Read system CPU metric, return map of attribute and value
  static absl::flat_hash_map<std::string, double> GetCpu()
      ABSL_LOCKS_EXCLUDED(mu_);

  // Read system Memory metric, return map of attribute and value
  static absl::flat_hash_map<std::string, double> GetMemory()
      ABSL_LOCKS_EXCLUDED(mu_);

  // Read system Thread metric, return map of attribute and value
  static absl::flat_hash_map<std::string, double> GetThread()
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  static inline absl::Mutex mu_;
  static inline pid_t inference_pid_ ABSL_GUARDED_BY(mu_) =
      -1;  // Default value as -1
};

namespace internal {
struct Utilization {
  double total;
  double self;
  std::optional<double> process;
};

// Use system cpu time from /proc/stat (`cpu_times`) /proc/self/stat
// (`self_stat_fields`) to calculate utilization &
// (`process_stat_fields`) to calculate utilization for inference sidecar
Utilization ReadCpuTime(
    const std::vector<size_t>& cpu_times,
    const std::vector<std::string>& self_fields,
    std::optional<std::vector<std::string>> process_fields = std::nullopt);
}  // namespace internal

}  // namespace privacy_sandbox::server_common

#endif  // SERVICES_COMMON_UTIL_READ_SYSTEM_H_
