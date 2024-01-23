//  Copyright 2023 Google LLC
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
#ifndef SERVICES_AUCTION_SERVICE_REPORTING_NOISER_AND_BUCKETER_H_
#define SERVICES_AUCTION_SERVICE_REPORTING_NOISER_AND_BUCKETER_H_

#include <cstdint>

#include "absl/random/random.h"
#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Generates a 64 bit unsigned random integer within [0,max).
absl::StatusOr<uint64_t> RandGenerator(uint64_t max);
uint8_t BucketJoinCount(int32_t join_count);
uint8_t BucketRecency(long recency);

// Stochastically rounds the floating point value to k bits.
absl::StatusOr<double> RoundStochasticallyToKBits(double value, unsigned k);

// Noises and masks ModelingSignals input for reportWin.
// modeling_signals are noised 1 in 100 times. If noised, a random integer
// between 0 and 0x0FFF  will be returned. Applies a mask of 0x0FFF to retain
// only 12 bits.
absl::StatusOr<uint16_t> NoiseAndMaskModelingSignals(uint16_t modeling_signals);

absl::StatusOr<uint8_t> NoiseAndBucketJoinCount(int32_t join_count);

// Converts `recency` to a 5-bit value (32 buckets), and randomly applies
// noise.
// Returns values in the range 0-31, inclusive.
absl::StatusOr<uint8_t> NoiseAndBucketRecency(long recency);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_REPORTING_NOISER_AND_BUCKETER_H_
