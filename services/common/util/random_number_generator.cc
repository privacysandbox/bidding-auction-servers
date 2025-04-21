//  Copyright 2025 Google LLC
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
//  limitations under the License

#include "services/common/util/random_number_generator.h"

#include <limits>

#include "openssl/rand.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<uint64_t> RandUint64() {
  uint64_t rand_64;
  if (RAND_bytes(reinterpret_cast<uint8_t*>(&rand_64), sizeof(rand_64)) != 1) {
    return absl::InternalError("Error generating number.");
  }
  return rand_64;
}

absl::StatusOr<uint64_t> RandGenerator(uint64_t range) {
  if (range <= 0) {
    return absl::InternalError("Invalid range for random number generation.");
  }
  // We must discard random results above this number, as they would
  // make the random generator non-uniform (consider e.g. if
  // MAX_UINT64 was 7 and range was 5, then a result of 1 would be twice
  // as likely as a result of 3 or 4).
  uint64_t max_acceptable_value =
      (std::numeric_limits<uint64_t>::max() / range) * range - 1;

  uint64_t rand_unit64;
  do {
    PS_ASSIGN_OR_RETURN(rand_unit64, RandUint64());
  } while (rand_unit64 > max_acceptable_value);

  return rand_unit64 % range;
}

absl::StatusOr<int> RandInt(int min, int max) {
  if (min > max) {
    return absl::InternalError("Invalid range for random number generation.");
  } else if (min == max) {
    return min;
  }
  uint64_t range = static_cast<uint64_t>(static_cast<int64_t>(max) -
                                         static_cast<int64_t>(min) + 1);
  // |range| is at most UINT_MAX + 1, so the result of RandGenerator(range)
  // is at most UINT_MAX. Hence it's safe to cast it from uint64_t to int64_t.
  int rand_unit64;
  PS_ASSIGN_OR_RETURN(rand_unit64, RandGenerator(range));
  return rand_unit64 + min;
}

}  // namespace privacy_sandbox::bidding_auction_servers
