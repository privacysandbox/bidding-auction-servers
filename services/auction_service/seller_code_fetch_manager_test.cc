// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/auction_service/seller_code_fetch_manager.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/util/file_util.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/public/cpio/interface/error_codes.h"
#include "src/public/cpio/mock/blob_storage_client/mock_blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using auction_service::FetchMode;
using auction_service::SellerCodeFetchConfig;

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::MockBlobStorageClient;
using ::testing::Return;

class SellerCodeFetchManagerTest : public testing::Test {
 protected:
  void SetUp() override {
    executor_ = std::make_unique<MockExecutor>();
    http_fetcher_ = std::make_unique<MockHttpFetcherAsync>();
    dispatcher_ = std::make_unique<MockV8Dispatcher>();
    blob_storage_client_ = std::make_unique<MockBlobStorageClient>();
  }
  std::unique_ptr<MockExecutor> executor_;
  std::unique_ptr<MockHttpFetcherAsync> http_fetcher_;
  std::unique_ptr<MockV8Dispatcher> dispatcher_;
  std::unique_ptr<MockBlobStorageClient> blob_storage_client_;
};

TEST_F(SellerCodeFetchManagerTest, FetchModeLocalTriesFileLoad) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(FetchMode::FETCH_MODE_LOCAL);
  const std::string bad_path = "error";
  const bool enable_protected_app_signals = true;

  udf_config.set_auction_js_path(bad_path);
  SellerCodeFetchManager udf_fetcher(std::move(blob_storage_client_),
                                     executor_.get(), http_fetcher_.get(),
                                     http_fetcher_.get(), dispatcher_.get(),
                                     udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(), absl::StrCat(kPathFailed, bad_path));
}

TEST_F(SellerCodeFetchManagerTest,
       FetchModeBucketSucceedsLoadingWrappedBucketBlobs) {
  std::string fake_udf = "udf_data";
  std::string pa_buyer_origin = "pa_origin";
  std::string pas_buyer_origin = "pas_origin";
  std::string pa_reporting_udf_data = "pa_reporting_udf_data";
  std::string pas_reporting_udf_data = "pas_reporting_udf_data";
  const bool enable_protected_app_signals = true;

  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(FetchMode::FETCH_MODE_BUCKET);
  udf_config.set_enable_report_win_url_generation(true);
  udf_config.set_enable_report_result_url_generation(true);
  udf_config.mutable_buyer_report_win_js_urls()->try_emplace(pa_buyer_origin,
                                                             "foo.com");
  udf_config.mutable_protected_app_signals_buyer_report_win_js_urls()
      ->try_emplace(pas_buyer_origin, "bar.com");
  udf_config.set_auction_js_bucket("js");
  udf_config.set_auction_js_bucket_default_blob("default");

  EXPECT_CALL(*blob_storage_client_, Init)
      .WillOnce(Return(SuccessExecutionResult()));
  EXPECT_CALL(*blob_storage_client_, Run)
      .WillOnce(Return(SuccessExecutionResult()));
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .Times(1)
      .WillOnce([&](const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url,
                  udf_config.buyer_report_win_js_urls().at(pa_buyer_origin));
        EXPECT_EQ(
            requests[1].url,
            udf_config.protected_app_signals_buyer_report_win_js_urls().at(
                pas_buyer_origin));

        std::move(done_callback)(
            {pa_reporting_udf_data, pas_reporting_udf_data});
      });

  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      udf_config.auction_js_bucket());
            BlobMetadata md;
            md.set_bucket_name(udf_config.auction_js_bucket());
            md.set_blob_name(udf_config.auction_js_bucket_default_blob());

            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return SuccessExecutionResult();
          });

  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&](AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, udf_config.auction_js_bucket());
            EXPECT_EQ(async_blob_name,
                      udf_config.auction_js_bucket_default_blob());

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(fake_udf);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return SuccessExecutionResult();
          });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .WillOnce([&](std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, udf_config.auction_js_bucket_default_blob());
        absl::flat_hash_map<std::string, std::string> pa_reporting_udfs;
        pa_reporting_udfs.try_emplace(pa_buyer_origin, pa_reporting_udf_data);
        absl::flat_hash_map<std::string, std::string> pas_reporting_udfs;
        pas_reporting_udfs.try_emplace(pas_buyer_origin,
                                       pas_reporting_udf_data);

        EXPECT_EQ(
            blob_data,
            GetSellerWrappedCode(
                fake_udf, udf_config.enable_report_result_url_generation(),
                enable_protected_app_signals,
                udf_config.enable_report_win_url_generation(),
                pa_reporting_udfs, pas_reporting_udfs));
        return absl::OkStatus();
      });

  SellerCodeFetchManager udf_fetcher(std::move(blob_storage_client_),
                                     executor_.get(), http_fetcher_.get(),
                                     http_fetcher_.get(), dispatcher_.get(),
                                     udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(SellerCodeFetchManagerTest, FetchModeUrlSucceedsLoadingWrappedUrlBlobs) {
  std::string fake_udf = "udf_data";
  std::string pa_buyer_origin = "pa_origin";
  std::string pas_buyer_origin = "pas_origin";
  std::string pa_reporting_udf_data = "pa_reporting_udf_data";
  std::string pas_reporting_udf_data = "pas_reporting_udf_data";
  const bool enable_protected_app_signals = true;

  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(FetchMode::FETCH_MODE_URL);
  udf_config.set_enable_report_win_url_generation(true);
  udf_config.set_enable_report_result_url_generation(true);
  udf_config.mutable_buyer_report_win_js_urls()->try_emplace(pa_buyer_origin,
                                                             "foo.com");
  udf_config.mutable_protected_app_signals_buyer_report_win_js_urls()
      ->try_emplace(pas_buyer_origin, "bar.com");
  udf_config.set_auction_js_url("auction.com");
  udf_config.set_url_fetch_timeout_ms(70000000);
  udf_config.set_url_fetch_period_ms(70000001);
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .Times(2)
      .WillOnce([&](const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url,
                  udf_config.buyer_report_win_js_urls().at(pa_buyer_origin));
        EXPECT_EQ(
            requests[1].url,
            udf_config.protected_app_signals_buyer_report_win_js_urls().at(
                pas_buyer_origin));

        std::move(done_callback)(
            {pa_reporting_udf_data, pas_reporting_udf_data});
      })
      .WillOnce([&](const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, udf_config.auction_js_url());
        std::move(done_callback)({fake_udf});
      });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .WillOnce([&](std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, kScoreAdBlobVersion);
        absl::flat_hash_map<std::string, std::string> pa_reporting_udfs;
        pa_reporting_udfs.try_emplace(pa_buyer_origin, pa_reporting_udf_data);
        absl::flat_hash_map<std::string, std::string> pas_reporting_udfs;
        pas_reporting_udfs.try_emplace(pas_buyer_origin,
                                       pas_reporting_udf_data);

        EXPECT_EQ(
            blob_data,
            GetSellerWrappedCode(
                fake_udf, udf_config.enable_report_result_url_generation(),
                enable_protected_app_signals,
                udf_config.enable_report_win_url_generation(),
                pa_reporting_udfs, pas_reporting_udfs));
        return absl::OkStatus();
      });

  SellerCodeFetchManager udf_fetcher(std::move(blob_storage_client_),
                                     executor_.get(), http_fetcher_.get(),
                                     http_fetcher_.get(), dispatcher_.get(),
                                     udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
