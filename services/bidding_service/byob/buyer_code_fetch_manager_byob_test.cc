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

#include "services/bidding_service/byob/buyer_code_fetch_manager_byob.h"

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/file_util.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using bidding_service::BuyerCodeFetchConfig;

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::MockBlobStorageClient;
using ::testing::Return;

class BuyerCodeFetchManagerByobTest : public testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    executor_ = std::make_unique<MockExecutor>();
    http_fetcher_ = std::make_unique<MockHttpFetcherAsync>();
    loader_ = std::make_unique<MockUdfCodeLoaderInterface>();
    blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  }

  std::unique_ptr<MockExecutor> executor_;
  std::unique_ptr<MockHttpFetcherAsync> http_fetcher_;
  std::unique_ptr<MockUdfCodeLoaderInterface> loader_;
  std::unique_ptr<MockBlobStorageClient> blob_storage_client_;
};

TEST_F(BuyerCodeFetchManagerByobTest, LocalModeTriesFileLoadExec) {
  const std::string pa_exec_path = "pa_exec";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_LOCAL);
  udf_config.set_bidding_js_path("pa_js");  // should be ignored
  udf_config.set_bidding_executable_path(pa_exec_path);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  BuyerCodeFetchManagerByob udf_fetcher(
      executor_.get(), http_fetcher_.get(), loader_.get(),
      std::move(blob_storage_client_), udf_config);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(), absl::StrCat(kPathFailed, pa_exec_path));
}

TEST_F(BuyerCodeFetchManagerByobTest, BucketModeFailsForNoPABucket) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));

  BuyerCodeFetchManagerByob udf_fetcher(
      executor_.get(), http_fetcher_.get(), loader_.get(),
      std::move(blob_storage_client_), udf_config);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyBucketName, kProtectedAuctionExecutableId));
}

TEST_F(BuyerCodeFetchManagerByobTest, BucketModeFetchesExecForPA) {
  const std::string pa_exec_bucket = "pa_exec";
  const std::string pa_exec_object = "pa_exec_test";
  const std::string pa_exec_data = "pa_exec_data";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_auction_bidding_js_bucket(
      "pa_js");  // should be ignored
  udf_config.set_protected_auction_bidding_js_bucket_default_blob(
      "pa_js_test");  // should be ignored
  udf_config.set_protected_auction_bidding_executable_bucket(pa_exec_bucket);
  udf_config.set_protected_auction_bidding_executable_bucket_default_blob(
      pa_exec_object);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&pa_exec_bucket, &pa_exec_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pa_exec_bucket);
            BlobMetadata md;
            md.set_bucket_name(pa_exec_bucket);
            md.set_blob_name(pa_exec_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&pa_exec_bucket, &pa_exec_object, &pa_exec_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pa_exec_bucket);
            EXPECT_EQ(async_blob_name, pa_exec_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pa_exec_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });
  EXPECT_CALL(*loader_, LoadSync)
      .WillOnce([&pa_exec_object, &pa_exec_data](std::string_view version,
                                                 absl::string_view blob_data) {
        EXPECT_EQ(version, pa_exec_object);
        EXPECT_EQ(blob_data, pa_exec_data);
        return absl::OkStatus();
      });

  BuyerCodeFetchManagerByob udf_fetcher(
      executor_.get(), http_fetcher_.get(), loader_.get(),
      std::move(blob_storage_client_), udf_config);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(BuyerCodeFetchManagerByobTest, UrlModeFailsForNoPAUrl) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_bidding_wasm_helper_url("pa_wasm");  // should be ignored

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*http_fetcher_, FetchUrls).Times(0);

  BuyerCodeFetchManagerByob udf_fetcher(
      executor_.get(), http_fetcher_.get(), loader_.get(),
      std::move(blob_storage_client_), udf_config);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyUrl, kProtectedAuctionExecutableUrlId));
}

TEST_F(BuyerCodeFetchManagerByobTest, UrlModeFetchesExecForPA) {
  const std::string pa_exec_url = "pa_exec";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_bidding_js_url("pa_js");             // should be ignored
  udf_config.set_bidding_wasm_helper_url("pa_wasm");  // should be ignored
  udf_config.set_bidding_executable_url(pa_exec_url);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*loader_, LoadSync)
      .WillRepeatedly(
          [](std::string_view version, absl::string_view blob_data) {
            return absl::OkStatus();
          });
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&pa_exec_url](const std::vector<HTTPRequest>& requests,
                               absl::Duration timeout,
                               OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 1);
        EXPECT_EQ(requests[0].url, pa_exec_url);
        std::move(done_callback)({""});
      });

  BuyerCodeFetchManagerByob udf_fetcher(
      executor_.get(), http_fetcher_.get(), loader_.get(),
      std::move(blob_storage_client_), udf_config);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
