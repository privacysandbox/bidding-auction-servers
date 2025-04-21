// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/chaffing/moving_median.h"

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace privacy_sandbox::bidding_auction_servers {

MovingMedian::MovingMedian(size_t window_size, float sampling_probability)
    : window_size_(window_size),
      median_(absl::FailedPreconditionError("Window is not full")),
      sampling_probability_(sampling_probability) {
  DCHECK_GT(window_size_, 0);
  mid_ = window_.end();
}

void MovingMedian::AddNumber(RandomNumberGenerator& rng, int value) {
  absl::MutexLock lock(&window_mutex_);

  // Determine if the given value should be sampled.
  if (window_data_.size() >= window_size_ &&
      (sampling_probability_ == 0 ||
       rng.GetUniformReal(0.0, 1.0) >= sampling_probability_)) {
    return;
  }

  window_.insert(value);
  window_data_.push_back(value);

  // If the window size exceeds the given size, remove the oldest element.
  if (window_.size() > window_size_) {
    // Remove the oldest element in the window (which is the first element in
    // the deque).
    int to_remove = window_data_.front();
    window_data_.pop_front();
    window_.erase(window_.find(to_remove));
  }

  // Reset mid_ to point to the median of the window.
  if (window_.size() == window_size_) {
    mid_ = std::next(window_.begin(), (window_size_ - 1) / 2);
  }

  median_ = CalculateMedian();
}

absl::StatusOr<int> MovingMedian::CalculateMedian()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(window_mutex_) {
  if (window_data_.size() < window_size_) {
    return absl::FailedPreconditionError("Window is not full.");
  }

  if (window_size_ % 2 == 0) {
    // Even window size: average the two middle elements
    return (static_cast<double>(*mid_) + *std::next(mid_)) / 2.0;
  } else {
    // Odd window size: return the middle element.
    return static_cast<double>(*mid_);
  }
}

absl::StatusOr<int> MovingMedian::GetMedian() const {
  absl::ReaderMutexLock window_lock(&window_mutex_);
  return median_;
}

bool MovingMedian::IsWindowFilled() const {
  absl::ReaderMutexLock window_lock(&window_mutex_);
  return (window_size_ == window_data_.size());
}

}  // namespace privacy_sandbox::bidding_auction_servers
