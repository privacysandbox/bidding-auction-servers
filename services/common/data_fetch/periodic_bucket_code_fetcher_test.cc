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

#include "services/common/data_fetch/periodic_bucket_code_fetcher.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/public/cpio/interface/error_codes.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kSampleBucketName[] = "BucketName";

constexpr char kSampleBlobName[] = "BlobName1";
constexpr char kSampleBlobName2[] = "BlobName2";
constexpr char kSampleBlobName3[] = "BlobName3";

constexpr char kSampleData[] = "test1";
constexpr char kSampleData2[] = "test2";
constexpr char kSampleData3[] = "test3";

constexpr absl::Duration kFetchPeriod = absl::Seconds(3);

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::BlobStorageClientInterface;
using ::google::scp::cpio::Cpio;
using ::google::scp::cpio::CpioOptions;
using ::google::scp::cpio::LogOption;
using ::google::scp::cpio::MockBlobStorageClient;
using ::testing::InSequence;

class PeriodicBucketCodeFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(PeriodicBucketCodeFetcherTest, LoadsWrappedResultIntoCodeLoader) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();
  std::string wrapper_string = "_wrapper_";
  auto wrapper = [&wrapper_string](const std::vector<std::string>& blobs) {
    return absl::StrCat(blobs.at(0), wrapper_string);
  };

  EXPECT_CALL(*blob_storage_client, ListBlobsMetadata)
      .WillOnce(
          [](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                 async_context) {
            EXPECT_EQ(async_context.request->blob_metadata().bucket_name(),
                      kSampleBucketName);
            BlobMetadata md;
            md.set_bucket_name(kSampleBucketName);
            md.set_blob_name(kSampleBlobName);
            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            async_context.response->mutable_blob_metadatas()->Add(
                std::move(md));
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([](const absl::Duration duration,
                   absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(kFetchPeriod, duration);
        server_common::TaskId id;
        return id;
      });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillOnce(
          [](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, kSampleBucketName);
            EXPECT_EQ(async_blob_name, kSampleBlobName);

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(kSampleData);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&wrapper_string](std::string_view version,
                                  absl::string_view blob_data) {
        EXPECT_EQ(blob_data, absl::StrCat(kSampleData, wrapper_string));
        return absl::OkStatus();
      });

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), blob_storage_client.get());
  auto status = bucket_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  bucket_fetcher.End();
}

TEST_F(PeriodicBucketCodeFetcherTest, LoadsAllBlobsInBucket) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  absl::BlockingCounter done_get_blob(2);

  auto wrapper = [](const std::vector<std::string>& blobs) {
    return blobs.at(0);
  };

  EXPECT_CALL(*blob_storage_client, ListBlobsMetadata)
      .WillOnce(
          [](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                 async_context) {
            EXPECT_EQ(async_context.request->blob_metadata().bucket_name(),
                      kSampleBucketName);
            BlobMetadata md;
            md.set_bucket_name(std::string(kSampleBucketName));
            md.set_blob_name(std::string(kSampleBlobName));
            BlobMetadata md2;
            md2.set_bucket_name(std::string(kSampleBucketName));
            md2.set_blob_name(std::string(kSampleBlobName2));
            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            async_context.response->mutable_blob_metadatas()->Add(
                std::move(md));
            async_context.response->mutable_blob_metadatas()->Add(
                std::move(md2));
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .Times(2)
      .WillRepeatedly(
          [&done_get_blob](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.response = std::make_shared<GetBlobResponse>();
            if (async_context.request->blob_metadata().blob_name() ==
                kSampleBlobName) {
              async_context.response->mutable_blob()->set_data(
                  std::string(kSampleData));
            } else {
              async_context.response->mutable_blob()->set_data(
                  std::string(kSampleData2));
            }
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            done_get_blob.DecrementCount();
            return absl::OkStatus();
          });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            server_common::TaskId id;
            return id;
          });

  EXPECT_CALL(dispatcher, LoadSync(kSampleBlobName, kSampleData))
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  EXPECT_CALL(dispatcher, LoadSync(kSampleBlobName2, kSampleData2))
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), blob_storage_client.get());
  auto status = bucket_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done_get_blob.Wait();
  bucket_fetcher.End();
}

TEST_F(PeriodicBucketCodeFetcherTest, ReturnsSuccessIfAtLeastOneBlobLoads) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  auto wrapper = [](const std::vector<std::string>& blobs) {
    return blobs.at(0);
  };

  EXPECT_CALL(*blob_storage_client, ListBlobsMetadata)
      .WillOnce([](AsyncContext<ListBlobsMetadataRequest,
                                ListBlobsMetadataResponse>
                       async_context) {
        EXPECT_EQ(async_context.request->blob_metadata().bucket_name(),
                  kSampleBucketName);
        BlobMetadata md;
        md.set_bucket_name(std::string(kSampleBucketName));
        md.set_blob_name(std::string(kSampleBlobName));
        BlobMetadata md2;
        md2.set_bucket_name(std::string(kSampleBucketName));
        md2.set_blob_name(std::string(kSampleBlobName2));
        BlobMetadata md3;
        md3.set_bucket_name(std::string(kSampleBucketName));
        md3.set_blob_name(std::string(kSampleBlobName3));
        async_context.response = std::make_shared<ListBlobsMetadataResponse>();
        async_context.response->mutable_blob_metadatas()->Add(std::move(md));
        async_context.response->mutable_blob_metadatas()->Add(std::move(md2));
        async_context.response->mutable_blob_metadatas()->Add(std::move(md3));
        async_context.result = SuccessExecutionResult();
        async_context.Finish();
        return absl::OkStatus();
      });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillRepeatedly(
          [](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.response = std::make_shared<GetBlobResponse>();
            if (async_context.request->blob_metadata().blob_name() ==
                kSampleBlobName) {
              async_context.result = FailureExecutionResult(SC_UNKNOWN);
            } else if (async_context.request->blob_metadata().blob_name() ==
                       kSampleBlobName2) {
              async_context.response->mutable_blob()->set_data(
                  std::string(kSampleData2));
              async_context.result = SuccessExecutionResult();
            } else {
              return absl::UnknownError("");
            }
            async_context.Finish();

            return absl::OkStatus();
          });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce(
          [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            EXPECT_EQ(duration, kFetchPeriod);
            server_common::TaskId id;
            return id;
          });

  EXPECT_CALL(dispatcher, LoadSync(kSampleBlobName, kSampleData)).Times(0);

  EXPECT_CALL(dispatcher, LoadSync(kSampleBlobName2, kSampleData2))
      .Times(1)
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        return absl::OkStatus();
      });

  EXPECT_CALL(dispatcher, LoadSync(kSampleBlobName, kSampleData3)).Times(0);

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), blob_storage_client.get());
  auto status = bucket_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  bucket_fetcher.End();
}

TEST_F(PeriodicBucketCodeFetcherTest, FailsStartupIfNoBlobLoadedSuccessfully) {
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();
  auto wrapper = [](const std::vector<std::string>& blobs) {
    return blobs.at(0);
  };

  EXPECT_CALL(*blob_storage_client, ListBlobsMetadata)
      .WillOnce(
          [](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                 async_context) {
            EXPECT_EQ(async_context.request->blob_metadata().bucket_name(),
                      kSampleBucketName);
            BlobMetadata md;
            md.set_bucket_name(kSampleBucketName);
            md.set_blob_name(kSampleBlobName);
            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            async_context.response->mutable_blob_metadatas()->Add(
                std::move(md));
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([](const absl::Duration duration,
                   absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(kFetchPeriod, duration);
        server_common::TaskId id;
        return id;
      });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillOnce(
          [](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, kSampleBucketName);
            EXPECT_EQ(async_blob_name, kSampleBlobName);

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(kSampleData);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(blob_data, kSampleData);
        return absl::UnavailableError("blob invalid");
      });

  PeriodicBucketCodeFetcher bucket_fetcher(
      kSampleBucketName, kFetchPeriod, &dispatcher, executor.get(),
      std::move(wrapper), blob_storage_client.get());
  EXPECT_FALSE(bucket_fetcher.Start().ok());
  bucket_fetcher.End();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
