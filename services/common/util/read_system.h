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

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace privacy_sandbox::server_common {

// Read system CPU metric, return map of attribute and value.
absl::flat_hash_map<std::string, double> GetCpu();

// Read system Memory metric, return map of attribute and value.
absl::flat_hash_map<std::string, double> GetMemory();

// Read system Thread metric, return map of attribute and value.
absl::flat_hash_map<std::string, double> GetThread();

namespace internal {
struct Utilization {
  double total;
  double self;
};

// Use system cpu time from /proc/stat (`cpu_times`) /proc/self/stat
// (`self_stat_fields`) to calculate utilization
Utilization ReadCpuTime(const std::vector<size_t>& cpu_times,
                        const std::vector<std::string>& fields);
}  // namespace internal

}  // namespace privacy_sandbox::server_common
