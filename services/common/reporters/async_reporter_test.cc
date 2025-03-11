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

#include "services/common/reporters/async_reporter.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "grpc/event_engine/event_engine.h"
#include "grpc/grpc.h"
#include "include/gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/test/constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

class AsyncReporterTest : public ::testing::Test {
 protected:
  AsyncReporterTest() {
    executor_ = std::make_unique<server_common::EventEngineExecutor>(
        grpc_event_engine::experimental::CreateEventEngine());
    reporter_ = std::make_unique<AsyncReporter>(
        std::make_unique<MultiCurlHttpFetcherAsync>(executor_.get()));
  }
  std::unique_ptr<server_common::EventEngineExecutor> executor_;
  std::unique_ptr<AsyncReporter> reporter_;
  server_common::GrpcInit gprc_init;
};

TEST_F(AsyncReporterTest, DoReportSuccessfully) {
  absl::BlockingCounter done(1);
  // NOLINTNEXTLINE
  auto done_cb = [&done](absl::StatusOr<absl::string_view> result) {
    done.DecrementCount();
    ASSERT_TRUE(result.ok()) << result.status();
    EXPECT_GT(result.value().length(), 0);
  };
  reporter_->DoReport({"https://wikipedia.org", {}}, done_cb);
  done.Wait();
}

TEST_F(AsyncReporterTest, DoReportEmptyUrl) {
  absl::BlockingCounter done(1);
  // NOLINTNEXTLINE
  auto done_cb = [&done](absl::StatusOr<absl::string_view> result) {
    done.DecrementCount();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  };
  reporter_->DoReport({"", {}}, done_cb);
  done.Wait();
}

TEST_F(AsyncReporterTest, DoReportMalformedUrl) {
  absl::BlockingCounter done(1);
  // NOLINTNEXTLINE
  auto done_cb = [&done](absl::StatusOr<absl::string_view> result) {
    done.DecrementCount();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
  };
  reporter_->DoReport({"https://random-website-which-does-not-exists.com", {}},
                      done_cb);
  done.Wait();
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
