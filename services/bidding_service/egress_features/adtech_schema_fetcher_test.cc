//  Copyright 2024 Google LLC
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

#include "services/bidding_service/egress_features/adtech_schema_fetcher.h"

#include <gmock/gmock-matchers.h>

#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/test/mocks.h"
#include "services/common/util/file_util.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::HasSubstr;
constexpr char kTestEgressSchema[] = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "bucket-feature",
          "size": 2
        }
      ]
    }
  )JSON";

class AdtechSchemaFetcherTest : public ::testing::Test {
 public:
  void SetUp() override {
    server_common::log::SetGlobalPSVLogLevel(10);
    auto cddl_spec_cache = std::make_unique<CddlSpecCache>(
        "services/bidding_service/egress_cddl_spec/");
    CHECK_OK(cddl_spec_cache->Init());
    egress_schema_cache_ =
        std::make_unique<EgressSchemaCacheMock>(std::move(cddl_spec_cache));
    adtech_schema_fetcher_ = std::make_unique<AdtechSchemaFetcher>(
        schema_endpoints, fetch_period_, time_out_, http_fetcher_.get(),
        executor_.get(), egress_schema_cache_.get());
  }

 protected:
  std::unique_ptr<EgressSchemaCacheMock> egress_schema_cache_;
  std::vector<std::string> schema_endpoints = {"egress_schema.com"};
  absl::Duration fetch_period_ = absl::Minutes(10);
  absl::Duration time_out_ = absl::Milliseconds(1000);
  std::unique_ptr<MockExecutor> executor_ = std::make_unique<MockExecutor>();
  std::unique_ptr<MockHttpFetcherAsync> http_fetcher_ =
      std::make_unique<MockHttpFetcherAsync>();
  std::unique_ptr<AdtechSchemaFetcher> adtech_schema_fetcher_;
};

TEST_F(AdtechSchemaFetcherTest, UpdatesEgressSchemaCacheOnFetch) {
  absl::Notification http_fetch_done;
  absl::Notification schema_cache_update_done;
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .Times(1)
      .WillOnce(
          [&http_fetch_done](const std::vector<HTTPRequest>& requests,
                             absl::Duration timeout,
                             absl::AnyInvocable<void(
                                 std::vector<absl::StatusOr<std::string>>)&&>
                                 done_callback) {
            http_fetch_done.Notify();
            std::move(done_callback)({kTestEgressSchema});
          });

  EXPECT_CALL(*executor_, RunAfter)
      .WillOnce(
          [this](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(fetch_period_, duration);
            return server_common::TaskId{};
          });

  EXPECT_CALL(*egress_schema_cache_, Update)
      .WillOnce([&schema_cache_update_done](absl::string_view egress_schema,
                                            absl::string_view schema_id) {
        EXPECT_EQ(egress_schema, kTestEgressSchema);
        schema_cache_update_done.Notify();
        return absl::OkStatus();
      });
  auto status = adtech_schema_fetcher_->Start();
  ASSERT_TRUE(status.ok()) << status;
  http_fetch_done.WaitForNotification();
  schema_cache_update_done.WaitForNotification();
}

TEST_F(AdtechSchemaFetcherTest, FetchConsideredAsFailedIfBadJsonFetched) {
  absl::Notification done;
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .Times(1)
      .WillOnce([&done](const std::vector<HTTPRequest>& requests,
                        absl::Duration timeout,
                        absl::AnyInvocable<void(
                            std::vector<absl::StatusOr<std::string>>)&&>
                            done_callback) {
        done.Notify();
        std::move(done_callback)({"Malformed JSON"});
      });

  EXPECT_CALL(*executor_, RunAfter)
      .WillOnce(
          [this](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(fetch_period_, duration);
            return server_common::TaskId{};
          });

  EXPECT_CALL(*egress_schema_cache_, Update)
      .WillOnce(
          [](absl::string_view egress_schema, absl::string_view schema_id) {
            EXPECT_EQ(egress_schema, "Malformed JSON");
            return absl::InvalidArgumentError("Failed to parse adtech schema");
          });
  auto status = adtech_schema_fetcher_->Start();
  ASSERT_FALSE(status.ok());
  EXPECT_THAT(status.message(), HasSubstr("No blob loaded successfully."));
  done.WaitForNotification();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
