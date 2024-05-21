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

#include "services/common/blob_fetch/blob_fetcher.h"

#include <memory>
#include <utility>

#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/interface/error_codes.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::MockBlobStorageClient;

constexpr char kSampleBucketName[] = "BucketName";
constexpr char kSampleBlobName[] = "blob_name";
constexpr char kSampleData[] = "test";

TEST(BlobFetcherTest, FetchBucket) {
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  EXPECT_CALL(*blob_storage_client, Run).WillOnce([&]() {
    return SuccessExecutionResult();
  });

  EXPECT_CALL(*executor, Run).WillOnce([&](absl::AnyInvocable<void()> closure) {
    closure();
  });

  EXPECT_CALL(*blob_storage_client, ListBlobsMetadata)
      .WillOnce(
          [&](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            EXPECT_EQ(async_bucket_name, kSampleBucketName);

            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            auto* blob_metadata = async_context.response->add_blob_metadatas();
            blob_metadata->set_bucket_name(kSampleBucketName);
            blob_metadata->set_blob_name(kSampleBlobName);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return SuccessExecutionResult();
          });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillOnce(
          [&](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
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

            return SuccessExecutionResult();
          });

  BlobFetcher bucket_fetcher(kSampleBucketName, executor.get(),
                             std::move(blob_storage_client));
  EXPECT_TRUE(bucket_fetcher.FetchSync().ok());
}

TEST(BlobFetcherTest, FetchBucket_Failure) {
  auto executor = std::make_unique<MockExecutor>();
  auto blob_storage_client = std::make_unique<MockBlobStorageClient>();

  EXPECT_CALL(*blob_storage_client, Run).WillOnce([&]() {
    return SuccessExecutionResult();
  });

  EXPECT_CALL(*executor, Run).WillOnce([&](absl::AnyInvocable<void()> closure) {
    closure();
  });

  EXPECT_CALL(*blob_storage_client, ListBlobsMetadata)
      .WillOnce(
          [&](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            EXPECT_EQ(async_bucket_name, kSampleBucketName);

            async_context.response =
                std::make_shared<ListBlobsMetadataResponse>();
            auto* blob_metadata = async_context.response->add_blob_metadatas();
            blob_metadata->set_bucket_name(kSampleBucketName);
            blob_metadata->set_blob_name(kSampleBlobName);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return SuccessExecutionResult();
          });

  EXPECT_CALL(*blob_storage_client, GetBlob)
      .WillOnce(
          [&](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.result = FailureExecutionResult(SC_UNKNOWN);
            async_context.Finish();

            return FailureExecutionResult(SC_UNKNOWN);
          });

  BlobFetcher bucket_fetcher(kSampleBucketName, executor.get(),
                             std::move(blob_storage_client));
  auto status = bucket_fetcher.FetchSync();
  EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
