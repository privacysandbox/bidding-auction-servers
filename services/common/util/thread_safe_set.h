/*
 * Copyright 2024 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_THREAD_SAFE_SET_H_
#define SERVICES_COMMON_UTIL_THREAD_SAFE_SET_H_

#include <algorithm>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace privacy_sandbox::bidding_auction_servers {

// Supports thread safe insertion and iteration for a hash set.
// Each access to the set requires a lock.
template <typename T>
class ThreadSafeSet {
 public:
  void Insert(T value) ABSL_LOCKS_EXCLUDED(set_mutex_) {
    absl::MutexLock lock(&set_mutex_);
    set_.insert(value);
  }

  void Erase(T value) ABSL_LOCKS_EXCLUDED(set_mutex_) {
    absl::MutexLock lock(&set_mutex_);
    set_.erase(value);
  }

  void ForEach(absl::AnyInvocable<void(T)> f) ABSL_LOCKS_EXCLUDED(set_mutex_) {
    absl::MutexLock lock(&set_mutex_);
    std::for_each(set_.begin(), set_.end(), std::move(f));
  }

  size_t Size() const ABSL_LOCKS_EXCLUDED(set_mutex_) {
    absl::MutexLock lock(&set_mutex_);
    return set_.size();
  }

 private:
  mutable absl::Mutex set_mutex_;
  absl::flat_hash_set<T> set_ ABSL_GUARDED_BY(set_mutex_);
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_THREAD_SAFE_SET_H_
