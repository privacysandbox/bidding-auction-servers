//  Copyright 202t Google LLC
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

#ifndef SERVICES_COMMON_UTIL_RANDOM_NUMBER_GENERATOR_H_
#define SERVICES_COMMON_UTIL_RANDOM_NUMBER_GENERATOR_H_

#include "absl/random/random.h"
#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Uses OpenSSL's RAND_bytes to generate a 64 bit unsigned random integer.
absl::StatusOr<uint64_t> RandUint64();

// Generates a 64 bit unsigned random integer within [0,max).
absl::StatusOr<uint64_t> RandGenerator(uint64_t max);

// Generates a random integer within the range of [min,max].
absl::StatusOr<int> RandInt(int min, int max);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_RANDOM_NUMBER_GENERATOR_H_
