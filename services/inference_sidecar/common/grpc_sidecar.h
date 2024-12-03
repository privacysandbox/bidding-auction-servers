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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_GRPC_SIDECAR_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_GRPC_SIDECAR_H_

#include "absl/status/status.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// ML models are reset at the probability of 0.1%.
constexpr double kMinResetProbability = 0.001;

absl::Status SetCpuAffinity(const InferenceSidecarRuntimeConfig& config);

// Makes sure model_reset_probability must be set to kMinResetProbability.
absl::Status EnforceModelResetProbability(
    InferenceSidecarRuntimeConfig& config);

// Sets the TCMalloc config.
absl::Status SetTcMallocConfig(const InferenceSidecarRuntimeConfig& config);

// Runs a simple gRPC server. It is thread safe.
absl::Status Run(const InferenceSidecarRuntimeConfig& config);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_GRPC_SIDECAR_H_
