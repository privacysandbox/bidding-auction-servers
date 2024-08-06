//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_CPU_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_CPU_H_

#include <vector>

#include "absl/status/status.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Set the CPU affinity to the current process given a list of CPU IDs.
absl::Status SetCpuAffinity(const std::vector<int>& cpus);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_CPU_H_
