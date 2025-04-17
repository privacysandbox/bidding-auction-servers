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

#ifndef SERVICES_COMMON_CHAFFING_MOVING_MEDIAN_MANAGER_H
#define SERVICES_COMMON_CHAFFING_MOVING_MEDIAN_MANAGER_H

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "services/common/chaffing/moving_median.h"
#include "services/common/random/rng.h"

namespace privacy_sandbox::bidding_auction_servers {

class MovingMedianManager {
 public:
  explicit MovingMedianManager(const absl::flat_hash_set<std::string>& buyers,
                               size_t window_size, float sampling_probability);

  virtual ~MovingMedianManager() {}

  // Adds a value to a buyer's moving median window.
  virtual absl::Status AddNumberToBuyerWindow(absl::string_view buyer,
                                              RandomNumberGenerator& rng,
                                              int val);

  // Fetches median of a buyer's moving median window.
  virtual absl::StatusOr<int> GetMedian(absl::string_view buyer) const;

  // Returns whether the specified buyer's moving median window is filled.
  virtual absl::StatusOr<bool> IsWindowFilled(absl::string_view buyer) const;

 private:
  absl::flat_hash_map<absl::string_view, MovingMedian> moving_medians_by_buyer_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CHAFFING_MOVING_MEDIAN_MANAGER_H
