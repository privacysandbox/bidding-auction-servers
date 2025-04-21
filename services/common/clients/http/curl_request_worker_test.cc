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

#include "services/common/clients/http/curl_request_worker.h"

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
#include "services/common/clients/http/curl_request_queue.h"
#include "services/common/test/utils/test_init.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr absl::Duration kBigWaitTime = absl::Minutes(30);

class CurlRequestWorkerTest : public ::testing::Test {
 protected:
  CurlRequestWorkerTest() {
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

TEST_F(CurlRequestWorkerTest, WorkerDqueuesWork) {
  CurlRequestQueue queue(executor_.get(), /*capacity=*/1, kBigWaitTime);
  CurlRequestWorker worker(executor_.get(), queue);
  {
    absl::MutexLock lock(&queue.Mu());
    queue.Enqueue(std::move(request_));
    ASSERT_FALSE(queue.Empty());
  }

  // Wait till the worker proceses elements in the queue.
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

TEST_F(CurlRequestWorkerTest, WorkersDqueueWork) {
  const int queue_capacity = 10;
  CurlRequestQueue queue(executor_.get(), /*capacity=*/queue_capacity,
                         kBigWaitTime);
  {
    absl::MutexLock lock(&queue.Mu());
    for (int i = 0; i < queue_capacity; ++i) {
      std::unique_ptr<CurlRequestData> request =
          std::make_unique<CurlRequestData>(
              // NOLINTNEXTLINE
              std::vector<std::string>{}, [](absl::StatusOr<HTTPResponse>) {},
              std::vector<std::string>{}, false);
      queue.Enqueue(std::move(request));
    }
    ASSERT_FALSE(queue.Empty());
  }

  std::vector<std::unique_ptr<CurlRequestWorker>> workers;
  // Note: Adding more workers than needed.
  workers.reserve(2UL * queue_capacity);
  for (int i = 0; i < 2 * queue_capacity; ++i) {
    workers.emplace_back(
        std::make_unique<CurlRequestWorker>(executor_.get(), queue));
  }

  // Wait till the worker proceses elements in the queue.
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
