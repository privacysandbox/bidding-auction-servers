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
//  limitations under the License.

#include "services/common/clients/http/curl_request_queue.h"

#include <string>
#include <utility>
#include <vector>

#include <include/gmock/gmock-matchers.h>

#include "absl/status/status.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "grpc/event_engine/event_engine.h"
#include "grpc/grpc.h"
#include "include/gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "rapidjson/document.h"
#include "services/common/clients/http/curl_request_data.h"
#include "services/common/test/utils/test_init.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::Duration kBigWaitTime = absl::Minutes(30);
constexpr absl::Duration kSmallWaitTime = absl::Milliseconds(1);

class CurlRequestQueueTest : public ::testing::Test {
 protected:
  CurlRequestQueueTest() {
    CommonTestInit();
    executor_ = std::make_unique<server_common::EventEngineExecutor>(
        grpc_event_engine::experimental::CreateEventEngine());
  }

  std::unique_ptr<server_common::EventEngineExecutor> executor_;
  std::unique_ptr<CurlRequestData> request_ = std::make_unique<CurlRequestData>(
      // NOLINTNEXTLINE
      std::vector<std::string>{}, [](absl::StatusOr<HTTPResponse>) {},
      std::vector<std::string>{}, false);
  server_common::GrpcInit gprc_init;
};

TEST_F(CurlRequestQueueTest, CanEnqueue) {
  CurlRequestQueue queue(executor_.get(), /*capacity=*/1, kBigWaitTime);
  {
    absl::MutexLock lock(&queue.Mu());
    ASSERT_TRUE(queue.Empty());
    queue.Enqueue(std::move(request_));
    EXPECT_FALSE(queue.Empty());
  }
}

TEST_F(CurlRequestQueueTest, CanDequeue) {
  CurlRequestQueue queue(executor_.get(), /*capacity=*/1, kBigWaitTime);
  {
    absl::MutexLock lock(&queue.Mu());
    queue.Enqueue(std::move(request_));
    auto request = queue.Dequeue();
    ASSERT_TRUE(request != nullptr);
  }
}

TEST_F(CurlRequestQueueTest, DequeueOnEmptyReturnsNullPtr) {
  CurlRequestQueue queue(executor_.get(), /*capacity=*/1, kBigWaitTime);
  absl::MutexLock lock(&queue.Mu());
  ASSERT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Dequeue(), nullptr);
}

TEST_F(CurlRequestQueueTest, RequestsTimeoutFromQueue) {
  CurlRequestQueue queue(executor_.get(), /*capacity=*/1, kSmallWaitTime);
  {
    absl::MutexLock lock(&queue.Mu());
    queue.Enqueue(std::move(request_));
    ASSERT_FALSE(queue.Empty());
  }

  absl::MutexLock lock(&queue.Mu(), absl::Condition(
                                        +[](CurlRequestQueue* q) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
                                          return q->Empty();
                                        },
                                        &queue));
#pragma clang diagnostic pop
  ASSERT_TRUE(queue.Empty());
  EXPECT_EQ(queue.Dequeue(), nullptr);
}

TEST_F(CurlRequestQueueTest, CanEnqueuDequeFromMultipleThreads) {
  int num_threads = 100;
  CurlRequestQueue queue(executor_.get(), /*capacity=*/num_threads,
                         kSmallWaitTime);
  std::vector<std::thread> enqueing_threads;
  enqueing_threads.reserve(num_threads);
  absl::BlockingCounter enqueue_counter(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    enqueing_threads.emplace_back([&enqueue_counter, &queue]() {
      absl::MutexLock lock(&queue.Mu());
      queue.Enqueue(std::make_unique<CurlRequestData>(
          // NOLINTNEXTLINE
          std::vector<std::string>{}, [](absl::StatusOr<HTTPResponse>) {},
          std::vector<std::string>{}, false));
      enqueue_counter.DecrementCount();
    });
  }
  for (auto& t : enqueing_threads) {
    t.join();
  }
  enqueue_counter.Wait();

  std::vector<std::thread> dequeing_threads;
  dequeing_threads.reserve(num_threads);
  absl::BlockingCounter dequeue_counter(num_threads);
  for (int i = 0; i < num_threads; ++i) {
    dequeing_threads.emplace_back([&dequeue_counter, &queue]() {
      absl::MutexLock lock(&queue.Mu());
      queue.Dequeue();
      dequeue_counter.DecrementCount();
    });
  }
  for (auto& t : dequeing_threads) {
    t.join();
  }
  dequeue_counter.Wait();

  absl::MutexLock lock(&queue.Mu(), absl::Condition(
                                        +[](CurlRequestQueue* q) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
                                          return q->Empty();
                                        },
                                        &queue));
#pragma clang diagnostic pop
  EXPECT_TRUE(queue.Empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
