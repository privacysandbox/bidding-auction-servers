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

#include "services/common/util/async_task_tracker.h"

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr int kNumMaxThreads = 10;

class AsyncTasksTrackerTest : public testing::Test {
 protected:
  RequestLogContext log_context_{absl::btree_map<std::string, std::string>{},
                                 server_common::ConsentedDebugConfiguration()};
  absl::Notification notification_;
};

TEST_F(AsyncTasksTrackerTest, CallsBackOnTaskSuccess) {
  AsyncTaskTracker async_task_tracker(1, log_context_,
                                      [this](bool any_successful) {
                                        EXPECT_TRUE(any_successful);
                                        notification_.Notify();
                                      });

  std::thread t([&async_task_tracker]() {
    async_task_tracker.TaskCompleted(TaskStatus::SUCCESS);
  });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(AsyncTasksTrackerTest, CallsBackOnTaskError) {
  AsyncTaskTracker async_task_tracker(1, log_context_,
                                      [this](bool any_successful) {
                                        EXPECT_FALSE(any_successful);
                                        notification_.Notify();
                                      });

  std::thread t([&async_task_tracker]() {
    async_task_tracker.TaskCompleted(TaskStatus::ERROR);
  });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(AsyncTasksTrackerTest, CallsBackOnTaskSkipped) {
  AsyncTaskTracker task_tracker(1, log_context_, [this](bool any_successful) {
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::thread t(
      [&task_tracker]() { task_tracker.TaskCompleted(TaskStatus::SKIPPED); });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(AsyncTasksTrackerTest, CallsBackOnTaskEmptyResponse) {
  AsyncTaskTracker task_tracker(1, log_context_, [this](bool any_successful) {
    // No task errored and one task successful =>
    // chaff to the caller.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::thread t([&task_tracker]() {
    task_tracker.TaskCompleted(TaskStatus::EMPTY_RESPONSE);
  });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(AsyncTasksTrackerTest, CallsBackOnCancelledResponse) {
  AsyncTaskTracker task_tracker(1, log_context_, [this](bool any_successful) {
    // No task errored and one task successful =>
    // chaff to the caller.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::thread t(
      [&task_tracker]() { task_tracker.TaskCompleted(TaskStatus::CANCELLED); });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(AsyncTasksTrackerTest, DeclaresSuccessIfAnyTaskSucceeded) {
  AsyncTaskTracker task_tracker(5, log_context_, [this](bool any_successful) {
    // At least one task succeeded => succeeded
    // overall.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::vector<std::thread> threads;
  for (auto status :
       {TaskStatus::SUCCESS, TaskStatus::ERROR, TaskStatus::SKIPPED,
        TaskStatus::EMPTY_RESPONSE, TaskStatus::CANCELLED}) {
    threads.emplace_back(
        [&task_tracker, status]() { task_tracker.TaskCompleted(status); });
  }

  notification_.WaitForNotification();
  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(AsyncTasksTrackerTest, DeclaresSuccessIfNoTaskExplicitlyFailed) {
  AsyncTaskTracker task_tracker(4, log_context_, [this](bool any_successful) {
    // No task errored (but there is either a
    // skipped or empty response) => succeeded
    // overall.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::vector<std::thread> threads;
  for (auto status : {TaskStatus::ERROR, TaskStatus::SKIPPED,
                      TaskStatus::EMPTY_RESPONSE, TaskStatus::CANCELLED}) {
    threads.emplace_back(
        [&task_tracker, status]() { task_tracker.TaskCompleted(status); });
  }

  notification_.WaitForNotification();
  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(AsyncTasksTrackerTest, CallbackCalledOnlyOnce) {
  int count = 0;
  AsyncTaskTracker task_tracker(kNumMaxThreads, log_context_,
                                [this, &count](bool any_successful) {
                                  // No task errored (but there is either a
                                  // skipped or empty response) => succeeded
                                  // overall.
                                  EXPECT_TRUE(any_successful);
                                  count++;
                                  notification_.Notify();
                                });

  std::vector<std::thread> threads;
  threads.reserve(kNumMaxThreads);
  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back(
        [&task_tracker]() { task_tracker.TaskCompleted(TaskStatus::SUCCESS); });
  }

  notification_.WaitForNotification();
  EXPECT_EQ(count, 1);
  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(AsyncTasksTrackerTest, OnSingleTaskDoneCalledWithLock) {
  AsyncTaskTracker task_tracker(
      kNumMaxThreads, log_context_,
      [this](bool any_successful) { notification_.Notify(); });

  std::vector<std::thread> threads;
  threads.reserve(kNumMaxThreads);
  int count = 0;
  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back([&task_tracker, &count]() {
      task_tracker.TaskCompleted(TaskStatus::SUCCESS, [&count]() { ++count; });
    });
  }

  notification_.WaitForNotification();
  EXPECT_EQ(count, 10);
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
