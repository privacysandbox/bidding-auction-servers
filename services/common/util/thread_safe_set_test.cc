// Copyright 2024 Google LLC
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

#include "services/common/util/thread_safe_set.h"

#include <thread>
#include <vector>

#include "include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr size_t kNumMaxThreads = 5;

TEST(ThreadSafeSetInsertTest, InsertsMultipleItemsConcurrently) {
  auto thread_safe_set = ThreadSafeSet<int>();

  std::vector<std::thread> threads;
  threads.reserve(kNumMaxThreads);
  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back(
        [&thread_safe_set, i]() { thread_safe_set.Insert(i); });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(thread_safe_set.Size(), kNumMaxThreads);
}

TEST(ThreadSafeSetEraseTest, ErasesMultipleItemsConcurrently) {
  auto thread_safe_set = ThreadSafeSet<int>();

  std::vector<std::thread> insert_threads;
  insert_threads.reserve(kNumMaxThreads);
  for (int i = 0; i < kNumMaxThreads; ++i) {
    insert_threads.emplace_back(
        [&thread_safe_set, i]() { thread_safe_set.Insert(i); });
  }

  for (auto& thread : insert_threads) {
    thread.join();
  }

  ASSERT_EQ(thread_safe_set.Size(), kNumMaxThreads);

  std::vector<std::thread> erase_threads;
  erase_threads.reserve(kNumMaxThreads);
  for (int i = 0; i < kNumMaxThreads; ++i) {
    erase_threads.emplace_back(
        [&thread_safe_set, i]() { thread_safe_set.Erase(i); });
  }

  for (auto& thread : erase_threads) {
    thread.join();
  }

  EXPECT_EQ(thread_safe_set.Size(), 0);
}

TEST(ThreadSafeSetForEachTest, ForEachIteratesOverAllElements) {
  auto thread_safe_set = ThreadSafeSet<int>();

  std::vector<std::thread> threads;
  threads.reserve(kNumMaxThreads);
  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back(
        [&thread_safe_set, i]() { thread_safe_set.Insert(i); });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  ASSERT_EQ(thread_safe_set.Size(), kNumMaxThreads);

  int counter = 0;
  thread_safe_set.ForEach([&counter](int x) { ++counter; });

  EXPECT_EQ(counter, kNumMaxThreads);
}

TEST(ThreadSafeSetInsertAndForEachTest, ConcurrentInsertAndForEach) {
  auto thread_safe_set = ThreadSafeSet<int>();

  std::vector<std::thread> threads;
  threads.reserve(3 * kNumMaxThreads);

  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back(
        [&thread_safe_set, i]() { thread_safe_set.Insert(i); });
  }

  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back(
        [&thread_safe_set, i]() { thread_safe_set.Insert(i); });
  }

  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back([&thread_safe_set]() {
      int counter = 0;
      thread_safe_set.ForEach([&counter](int x) { ++counter; });
      EXPECT_LE(counter, kNumMaxThreads);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(thread_safe_set.Size(), kNumMaxThreads);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
