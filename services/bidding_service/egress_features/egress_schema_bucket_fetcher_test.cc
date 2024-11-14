/*
 * Copyright 2024 Google LLC
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

#include "services/bidding_service/egress_features/egress_schema_bucket_fetcher.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::testing::_;
using ::testing::Eq;
using ::testing::Return;

constexpr absl::string_view kTestBucket = "test";
constexpr absl::string_view kSchema1BlobName = "schema1.json";
constexpr absl::string_view kSchema2BlobName = "schema2.json";
constexpr absl::string_view kSchema3BlobName = "schema3.json";

constexpr char kSchema[] = R"JSON(
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

class EgressSchemaBucketFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    auto cddl_spec_cache = std::make_unique<CddlSpecCache>(
        "services/bidding_service/egress_cddl_spec/");
    CHECK_OK(cddl_spec_cache->Init());
    egress_schema_cache_ =
        std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache));
    blob_storage_client_ =
        std::make_unique<google::scp::cpio::MockBlobStorageClient>();

    EXPECT_CALL(*executor_, RunAfter)
        .WillOnce(
            [](const absl::Duration duration, absl::AnyInvocable<void()> cb) {
              server_common::TaskId id;
              return id;
            });

    fetcher_ = std::make_unique<EgressSchemaBucketFetcher>(
        kTestBucket, absl::Milliseconds(1000), executor_.get(),
        blob_storage_client_.get(), egress_schema_cache_.get());
  }

  std::unique_ptr<MockExecutor> executor_ = std::make_unique<MockExecutor>();
  std::unique_ptr<google::scp::cpio::MockBlobStorageClient>
      blob_storage_client_;
  std::unique_ptr<EgressSchemaCache> egress_schema_cache_;
  std::unique_ptr<EgressSchemaBucketFetcher> fetcher_;
};

TEST_F(EgressSchemaBucketFetcherTest, UpdatesCacheWithSuccessfulFetches) {
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce([](AsyncContext<ListBlobsMetadataRequest,
                                ListBlobsMetadataResponse>
                       async_context) {
        async_context.response = std::make_shared<ListBlobsMetadataResponse>();
        BlobMetadata blob1;
        blob1.set_bucket_name(kTestBucket);
        blob1.set_blob_name(kSchema1BlobName);
        BlobMetadata blob2;
        blob2.set_bucket_name(kTestBucket);
        blob2.set_blob_name(kSchema2BlobName);
        BlobMetadata blob3;
        blob3.set_bucket_name(kTestBucket);
        blob3.set_blob_name(kSchema3BlobName);
        async_context.response->mutable_blob_metadatas()->Add(std::move(blob1));
        async_context.response->mutable_blob_metadatas()->Add(std::move(blob2));
        async_context.response->mutable_blob_metadatas()->Add(std::move(blob3));
        async_context.result = SuccessExecutionResult();
        async_context.Finish();

        return absl::OkStatus();
      });

  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .Times(3)
      .WillRepeatedly(
          [](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            async_context.response = std::make_shared<GetBlobResponse>();
            if (async_context.request->blob_metadata().blob_name() ==
                kSchema1BlobName) {
              async_context.result = FailureExecutionResult(SC_UNKNOWN);
              async_context.Finish();
              return absl::UnknownError("");
            } else {
              async_context.response->mutable_blob()->set_data(kSchema);
              async_context.result = SuccessExecutionResult();
              async_context.Finish();
              return absl::OkStatus();
            }
          });

  auto fetch_status = fetcher_->Start();
  ASSERT_TRUE(fetch_status.ok()) << fetch_status;
  fetcher_->End();
  auto first_schema = egress_schema_cache_->Get(
      *GetBucketBlobVersion(kTestBucket, kSchema1BlobName));
  ASSERT_EQ(first_schema.status().code(), absl::StatusCode::kInvalidArgument);
  auto second_schema = egress_schema_cache_->Get(
      *GetBucketBlobVersion(kTestBucket, kSchema2BlobName));
  ASSERT_TRUE(second_schema.ok());
  ASSERT_EQ(second_schema->version, 2);
  auto third_schema = egress_schema_cache_->Get(
      *GetBucketBlobVersion(kTestBucket, kSchema3BlobName));
  ASSERT_TRUE(third_schema.ok());
  ASSERT_EQ(third_schema->version, 2);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
