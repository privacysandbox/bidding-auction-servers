/*
 * Copyright 2025 Google LLC
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

#include "services/common/blob_storage_client/blob_storage_client_cpio.h"

#include <utility>

#include "gtest/gtest.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_cpio_utils.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::MockBlobStorageClient;

constexpr char kSampleBucketName[] = "BucketName";
constexpr char kSampleBlobName[] = "BlobName";
constexpr char kSampleData[] = "SampleData";

class BlobStorageClientCpioTest : public testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(BlobStorageClientCpioTest, GetBlobCallClientGetBlobFunction) {
  auto get_blob_request = std::make_shared<
      google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest>();
  auto blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  AsyncContext<google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
               google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>
      get_blobs_context(
          get_blob_request,
          [&](const AsyncContext<
              google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
              google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
                  get_blobs_context) { return absl::OkStatus(); });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .Times(1)
      .WillOnce(
          [](const AsyncContext<
              google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
              google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
                 get_blobs_context) { return absl::OkStatus(); });
  CpioBlobStorageClient cpio_client =
      CpioBlobStorageClient(std::move(blob_storage_client_));
  auto res = GetBlobFromResultCpio(cpio_client.GetBlob(get_blobs_context));

  EXPECT_TRUE(res.ok());
}

TEST_F(BlobStorageClientCpioTest, GetBlobReturnExpectedBlob) {
  auto get_blob_request = std::make_shared<
      google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest>();
  auto blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  get_blob_request->mutable_blob_metadata()->set_bucket_name(kSampleBucketName);
  get_blob_request->mutable_blob_metadata()->set_blob_name(kSampleBlobName);
  std::string data;
  AsyncContext<google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
               google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>
      get_blob_context(
          get_blob_request,
          [&data](const AsyncContext<
                  google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
                  google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
                      get_blob_context) {
            data = get_blob_context.response->blob().data();
            return absl::OkStatus();
          });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .Times(1)
      .WillOnce(
          [](AsyncContext<
              google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
              google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>
                 get_blob_context) {
            auto get_blob_bucket_name =
                get_blob_context.request->blob_metadata().bucket_name();
            auto get_blob_blob_name =
                get_blob_context.request->blob_metadata().blob_name();
            EXPECT_EQ(get_blob_bucket_name, kSampleBucketName);
            EXPECT_EQ(get_blob_blob_name, kSampleBlobName);
            get_blob_context.response = std::make_shared<
                google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>();
            get_blob_context.response->mutable_blob()->set_data(kSampleData);
            get_blob_context.result = SuccessExecutionResult();
            get_blob_context.Finish();

            return absl::OkStatus();
          });
  CpioBlobStorageClient cpio_client =
      CpioBlobStorageClient(std::move(blob_storage_client_));
  auto res = GetBlobFromResultCpio(cpio_client.GetBlob(get_blob_context));

  EXPECT_TRUE(res.ok());
  EXPECT_EQ(data, kSampleData);
}

TEST_F(BlobStorageClientCpioTest, GetBlobFailWhenClientGetBlobFail) {
  auto get_blob_request = std::make_shared<
      google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest>();
  auto blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  AsyncContext<google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
               google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>
      get_blobs_context(
          get_blob_request,
          [&](const AsyncContext<
              google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
              google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
                  get_blobs_context) { return absl::OkStatus(); });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .Times(1)
      .WillOnce(
          [](const AsyncContext<
              google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
              google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
                 get_blobs_context) {
            return absl::NotFoundError("NOT_FOUND");
          });
  CpioBlobStorageClient cpio_client =
      CpioBlobStorageClient(std::move(blob_storage_client_));
  auto res = GetBlobFromResultCpio(cpio_client.GetBlob(get_blobs_context));
  EXPECT_TRUE(!res.ok());
}

TEST_F(BlobStorageClientCpioTest,
       ListBlobsMetadataCallClientListBlobsMetadataFunction) {
  auto list_blob_metadata_request = std::make_shared<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest>();
  auto blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  AsyncContext<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>
      list_blobs_context(
          list_blob_metadata_request,
          [&](const AsyncContext<google::cmrt::sdk::blob_storage_service::v1::
                                     ListBlobsMetadataRequest,
                                 google::cmrt::sdk::blob_storage_service::v1::
                                     ListBlobsMetadataResponse>&
                  list_blobs_context) { return absl::OkStatus(); });
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .Times(1)
      .WillOnce([](const AsyncContext<google::cmrt::sdk::blob_storage_service::
                                          v1::ListBlobsMetadataRequest,
                                      google::cmrt::sdk::blob_storage_service::
                                          v1::ListBlobsMetadataResponse>&
                       list_blobs_context) { return absl::OkStatus(); });
  CpioBlobStorageClient cpio_client =
      CpioBlobStorageClient(std::move(blob_storage_client_));
  auto res = ListBlobsMetadataFromResultCpio(
      cpio_client.ListBlobsMetadata(list_blobs_context));
  EXPECT_TRUE(res.ok());
}

TEST_F(BlobStorageClientCpioTest,
       ListBlobsMetadataFailWhenClientListBlobsMetadataFail) {
  auto list_blob_metadata_request = std::make_shared<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest>();
  auto blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  AsyncContext<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>
      list_blobs_context(
          list_blob_metadata_request,
          [&](const AsyncContext<google::cmrt::sdk::blob_storage_service::v1::
                                     ListBlobsMetadataRequest,
                                 google::cmrt::sdk::blob_storage_service::v1::
                                     ListBlobsMetadataResponse>&
                  list_blobs_context) { return absl::OkStatus(); });
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .Times(1)
      .WillOnce([](const AsyncContext<google::cmrt::sdk::blob_storage_service::
                                          v1::ListBlobsMetadataRequest,
                                      google::cmrt::sdk::blob_storage_service::
                                          v1::ListBlobsMetadataResponse>&
                       list_blobs_context) {
        return absl::NotFoundError("NOT_FOUND");
      });
  CpioBlobStorageClient cpio_client =
      CpioBlobStorageClient(std::move(blob_storage_client_));
  auto res = ListBlobsMetadataFromResultCpio(
      cpio_client.ListBlobsMetadata(list_blobs_context));
  EXPECT_TRUE(!res.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
