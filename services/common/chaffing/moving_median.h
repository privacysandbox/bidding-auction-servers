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

#ifndef SERVICES_COMMON_CHAFFING_MOVING_MEDIAN_H_
#define SERVICES_COMMON_CHAFFING_MOVING_MEDIAN_H_

#include <deque>
#include <random>
#include <set>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "services/common/random/rng.h"

namespace privacy_sandbox::bidding_auction_servers {

// MovingMedian maintains a sliding window a fixed size and provides methods to
// add elements to the window and retrieve the median of the window.
// The constructor accepts a sampling_probability, which represents the
// probablity that (once the window of size window_size) a call to AddNumber()
// will add the number to the window. It is expected that there will be a much
// higher ratio of GetMedian() to AddNumber() calls when using this class.
class MovingMedian {
 public:
  explicit MovingMedian(size_t window_size, float sampling_probability);

  // Assumes the current window is empty/not in use (and doesn't set mid_).
  MovingMedian(MovingMedian&& other) noexcept
      : window_size_(other.window_size_),
        window_mutex_(),
        window_(std::move(other.window_)),
        window_data_(std::move(other.window_data_)),
        mid_(window_.end()),
        median_(std::move(other.median_)),
        sampling_probability_(other.sampling_probability_) {}

  // Inserts a new value into the sliding window. If the window reaches its
  // maximum size, the oldest value is removed.
  void AddNumber(RandomNumberGenerator& rng, int value);

  // Returns median of the window IF the window is full, otherwise an error.
  absl::StatusOr<int> GetMedian() const;

  bool IsWindowFilled() const;

 private:
  size_t window_size_;

  // Mutex for the data structures to maintain the moving window.
  mutable absl::Mutex window_mutex_;
  // Holds all values in the window.
  std::multiset<int> window_ ABSL_GUARDED_BY(window_mutex_);
  // Maintains the insertion order of elements in the window.
  std::deque<int> window_data_ ABSL_GUARDED_BY(window_mutex_);

  // Iterator pointing to the middle element of the window.
  std::multiset<int>::const_iterator mid_ ABSL_GUARDED_BY(window_mutex_);

  absl::StatusOr<int> CalculateMedian()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(window_mutex_);

  absl::StatusOr<int> median_ ABSL_GUARDED_BY(window_mutex_);

  float sampling_probability_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CHAFFING_MOVING_MEDIAN_H_
