/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/common/code_fetch/periodic_bucket_fetcher.h"

#include <utility>
#include <vector>

#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "scp/cc/core/interface/async_context.h"
#include "scp/cc/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "scp/cc/public/cpio/interface/cpio.h"
#include "scp/cc/public/cpio/interface/error_codes.h"
#include "scp/cc/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"
#include "services/common/test/mocks.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::BlobStorageClientInterface;
using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::google::scp::cpio::MockBlobStorageClient;

TEST(PeriodicBucketFetcherTest, LoadsResultIntoV8Dispatcher) {
  MockV8Dispatcher dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  constexpr char kSampleBucketName[] = "BucketName";
  constexpr char kSampleBlobName[] = "BlobName";
  constexpr char kSampleData[] = "test";

  absl::BlockingCounter done_load_sync(1);

  EXPECT_CALL(*blob_storage_client, Init).WillOnce([&]() {
    return SuccessExecutionResult();
  });

  EXPECT_CALL(*blob_storage_client, Run).WillOnce([&]() {
    return SuccessExecutionResult();
  });

  EXPECT_CALL(*executor, Run).WillOnce([&](absl::AnyInvocable<void()> closure) {
    closure();
  });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillOnce(
          [&kSampleBucketName, &kSampleBlobName](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, kSampleBucketName);
            EXPECT_EQ(async_blob_name, kSampleBlobName);

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data("test");
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return SuccessExecutionResult();
          });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&done_load_sync, &kSampleData](int version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(blob_data, kSampleData);
        done_load_sync.DecrementCount();
        return absl::OkStatus();
      });

  PeriodicBucketFetcher bucket_fetcher(
      "BucketName", "BlobName", absl::Milliseconds(3000), dispatcher,
      executor.get(), std::move(blob_storage_client));
  bucket_fetcher.Start();
  done_load_sync.Wait();
  bucket_fetcher.End();
}

TEST(PeriodicBucketFetcherTest, PeriodicallyFetchesBucket) {
  MockV8Dispatcher dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  absl::Duration fetch_period_ms = absl::Milliseconds(3000);
  absl::BlockingCounter done_get_blob(1);

  EXPECT_CALL(*blob_storage_client, Init).WillOnce([&]() {
    return SuccessExecutionResult();
  });
  EXPECT_CALL(*blob_storage_client, Run).WillOnce([&]() {
    return SuccessExecutionResult();
  });

  EXPECT_CALL(*executor, Run).WillOnce([&](absl::AnyInvocable<void()> closure) {
    closure();
  });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillOnce(
          [&done_get_blob](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data("test");

            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            done_get_blob.DecrementCount();
            return SuccessExecutionResult();
          });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([&fetch_period_ms](absl::Duration duration,
                                   absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(duration, fetch_period_ms);
        server_common::TaskId id;
        return id;
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&](int version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  PeriodicBucketFetcher bucket_fetcher(
      "BucketName", "BlobName", absl::Milliseconds(3000), dispatcher,
      executor.get(), std::move(blob_storage_client));
  bucket_fetcher.Start();
  done_get_blob.Wait();
  bucket_fetcher.End();
}

TEST(PeriodicBucketFetcherTest, LoadsOnlyDifferentFetchedResult) {
  MockV8Dispatcher dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  constexpr char kSampleData[] = "test";
  absl::BlockingCounter done_get_blob(2);
  absl::BlockingCounter done_load_sync(1);

  EXPECT_CALL(*blob_storage_client, Init).WillOnce([&]() {
    return SuccessExecutionResult();
  });
  EXPECT_CALL(*blob_storage_client, Run).WillOnce([&]() {
    return SuccessExecutionResult();
  });

  EXPECT_CALL(*executor, Run).WillOnce([&](absl::AnyInvocable<void()> closure) {
    closure();
  });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .Times(2)
      .WillRepeatedly(
          [&done_get_blob](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data("test");
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            done_get_blob.DecrementCount();
            return SuccessExecutionResult();
          });

  EXPECT_CALL(*executor, RunAfter)
      .Times(2)
      .WillOnce(
          [&](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            closure();
            server_common::TaskId id;
            return id;
          });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&done_load_sync, &kSampleData](int version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(blob_data, kSampleData);
        done_load_sync.DecrementCount();
        return absl::OkStatus();
      });

  PeriodicBucketFetcher bucket_fetcher(
      "BucketName", "BlobName", absl::Milliseconds(3000), dispatcher,
      executor.get(), std::move(blob_storage_client));
  bucket_fetcher.Start();
  done_get_blob.Wait();
  bucket_fetcher.End();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
