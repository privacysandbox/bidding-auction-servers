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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_BENCHMARK_REQUEST_UTILS_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_BENCHMARK_REQUEST_UTILS_H_

#include <cstdlib>
#include <string>

#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Creates a new RegisterModelRequest with the given model path.
RegisterModelRequest CreateRegisterModelRequest(
    const RegisterModelRequest& register_request,
    const std::string& new_model_path);

inline std::string GenerateRandomFloat() {
  return std::to_string(static_cast<float>(rand()) /  // NOLINT
                        static_cast<float>(RAND_MAX));
}

// Returns an array of random floating numbers.
std::string GenerateRandomFloats(int n);

std::string StringFormat(const std::string& s);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_BENCHMARK_REQUEST_UTILS_H_
