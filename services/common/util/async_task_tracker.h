/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_UTIL_ASYNC_TASK_TRACKER_H_
#define SERVICES_COMMON_UTIL_ASYNC_TASK_TRACKER_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {

enum class TaskStatus : std::uint8_t {
  UNKNOWN,
  SKIPPED,         // Task skipped.
  EMPTY_RESPONSE,  // Task received an empty response.
  CANCELLED,       // Task cancelled.
  ERROR,
  SUCCESS,
};

// Helper class to facilitate the clients to wait for all tasks to be
// completed before executing the registered callback. Clients need to ensure
// that the provided logger outlives instance of `AsyncTaskTracker` class.
//
// To use this module, user must know upfront how many tasks will be tracked
// by this module. This count is provided upfront when instantiating an object
// of this class. Once the initial task count is initialized, this class expects
// the client to update this object about the completion status of every task.
// This update can be done from any thread (this class makes sure that the count
// of pending tasks is updated in a thread safe fashion). When updating the
// task completion status, client can also provide a closure to run (again in
// a thread safe manner). Once all the tasks have been completed, this module
// callbacks the initially registered `on_all_tasks_done` callback. This final
// callback is called without holding any locks.
class AsyncTaskTracker {
 public:
  explicit AsyncTaskTracker(
      int num_tasks_to_track, RequestLogContext& log_context,
      absl::AnyInvocable<void(bool) &&> on_all_tasks_done);

  // Updates the stats. If all bids have been completed, then the registered
  // callback is called.
  void TaskCompleted(TaskStatus task_status) ABSL_LOCKS_EXCLUDED(mu_);

  // Updates the stats. If all bids have been completed, then the registered
  // callback is called. `on_single_task_done`, if provided, will be called with
  // a lock.
  void TaskCompleted(
      TaskStatus task_status,
      std::optional<absl::AnyInvocable<void()>> on_single_task_done)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Sets the number of tasks to track.
  void SetNumTasksToTrack(int num_tasks_to_track);

 private:
  // Indicates whether any bid was successful or if no buyer returned an empty
  // bid so that we should send a chaff back. This should be called after all
  // the get bid calls to buyer frontend have returned.
  bool AnyTaskSuccessfullyCompleted() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  std::string ToString() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  int num_tasks_to_track_;
  absl::Mutex mu_;
  int pending_tasks_count_ ABSL_GUARDED_BY(mu_);
  int successful_tasks_count_ ABSL_GUARDED_BY(mu_);
  int empty_tasks_count_ ABSL_GUARDED_BY(mu_);
  int skipped_tasks_count_ ABSL_GUARDED_BY(mu_);
  int error_tasks_count_ ABSL_GUARDED_BY(mu_);
  int cancelled_tasks_count_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(bool) &&> on_all_tasks_done_;
  RequestLogContext& log_context_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_ASYNC_TASK_TRACKER_H_
