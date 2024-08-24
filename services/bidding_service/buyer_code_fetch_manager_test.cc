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

#include "services/bidding_service/buyer_code_fetch_manager.h"

#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/file_util.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/interface/cpio.h"
#include "src/public/cpio/interface/error_codes.h"
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
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::FailureExecutionResult;
using ::google::scp::core::SuccessExecutionResult;
using ::google::scp::cpio::MockBlobStorageClient;
using ::testing::Return;

class BuyerCodeFetchManagerTest : public testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
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

TEST_F(BuyerCodeFetchManagerTest, FetchModeLocalTriesFileLoad) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_LOCAL);
  const std::string bad_path = "error";
  udf_config.set_bidding_js_path(bad_path);
  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    dispatcher_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    false /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(), absl::StrCat(kPathFailed, bad_path));
}

TEST_F(BuyerCodeFetchManagerTest, FetchModeBucketTriesBucketLoad) {
  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    dispatcher_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyBucketName, kProtectedAuctionJsId));
}

TEST_F(BuyerCodeFetchManagerTest, TriesBucketFetchForProtectedAuction) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_auction_bidding_js_bucket("pa");
  udf_config.set_protected_auction_bidding_js_bucket_default_blob("pa_test");

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                 context) {
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    dispatcher_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    false /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_FALSE(load_status.ok());
  EXPECT_TRUE(absl::StrContains(
      load_status.message(),
      absl::StrCat(kFailedBucketFetchStartup, kProtectedAuctionJsId, " pa")));
}

TEST_F(BuyerCodeFetchManagerTest, TriesBucketFetchForProtectedAppSignals) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_app_signals_bidding_js_bucket("pas");
  udf_config.set_protected_app_signals_bidding_js_bucket_default_blob(
      "pas_test");

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [](AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                 context) {
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    dispatcher_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_FALSE(load_status.ok());
  EXPECT_TRUE(absl::StrContains(
      load_status.message(), absl::StrCat(kFailedBucketFetchStartup,
                                          kProtectedAppSignalsJsId, " pas")));
}

TEST_F(BuyerCodeFetchManagerTest,
       TriesBucketFetchForAdsRetrievalAfterProtectedAppSignalsFetchSuccess) {
  const std::string pas_bucket = "pas";
  const std::string pas_object = "pas_test";
  const std::string pas_data = "sample_data";
  const std::string ads_bucket = "ads_retrieval";
  const std::string ads_object = "ads_test";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_app_signals_bidding_js_bucket(pas_bucket);
  udf_config.set_protected_app_signals_bidding_js_bucket_default_blob(
      pas_object);
  udf_config.set_ads_retrieval_js_bucket(ads_bucket);
  udf_config.set_ads_retrieval_bucket_default_blob(ads_object);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&pas_bucket, &pas_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pas_bucket);
            BlobMetadata md;
            md.set_bucket_name(pas_bucket);
            md.set_blob_name(pas_object);

            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          })
      .WillOnce([&ads_bucket](
                    const AsyncContext<ListBlobsMetadataRequest,
                                       ListBlobsMetadataResponse>& context) {
        EXPECT_EQ(context.request->blob_metadata().bucket_name(), ads_bucket);
        return absl::UnknownError("");
      });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&pas_bucket, &pas_object, &pas_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pas_bucket);
            EXPECT_EQ(async_blob_name, pas_object);

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pas_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          });
  BuyerCodeWrapperConfig wrapper_config = {
      .auction_type = AuctionType::kProtectedAppSignals,
      .auction_specific_setup = kEncodedProtectedAppSignalsHandler};
  EXPECT_CALL(*dispatcher_, LoadSync)
      .WillOnce([&pas_object, &pas_data, &wrapper_config](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, pas_object);
        EXPECT_EQ(blob_data, GetBuyerWrappedCode(pas_data, wrapper_config));
        return absl::OkStatus();
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    dispatcher_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();

  EXPECT_TRUE(absl::StrContains(
      load_status.message(), absl::StrCat(kFailedBucketFetchStartup,
                                          kAdsRetrievalJsId, " ", ads_bucket)));
}

TEST_F(BuyerCodeFetchManagerTest, FetchModeUrlTriesUrlLoad) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  const std::string pa_url = "a";
  const std::string pa_wasm_url = "b";
  const std::string pas_url = "c";
  const std::string pas_wasm_url = "d";
  const std::string ads_retrieval_url = "e";
  const std::string ads_retrieval_wasm_url = "f";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);

  udf_config.set_bidding_js_url(pa_url);
  udf_config.set_bidding_wasm_helper_url(pa_wasm_url);

  udf_config.set_protected_app_signals_bidding_js_url(pas_url);
  udf_config.set_protected_app_signals_bidding_wasm_helper_url(pas_wasm_url);

  udf_config.set_prepare_data_for_ads_retrieval_js_url(ads_retrieval_url);
  udf_config.set_prepare_data_for_ads_retrieval_wasm_helper_url(
      ads_retrieval_wasm_url);

  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);

  EXPECT_CALL(*dispatcher_, LoadSync)
      .WillRepeatedly(
          [](std::string_view version, absl::string_view blob_data) {
            return absl::OkStatus();
          });

  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&pa_wasm_url, &pa_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, pa_url);
        EXPECT_EQ(requests[1].url, pa_wasm_url);
        std::move(done_callback)({""});
      })
      .WillOnce([&pas_url, &pas_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, pas_url);
        EXPECT_EQ(requests[1].url, pas_wasm_url);
        std::move(done_callback)({""});
      })
      .WillOnce([&ads_retrieval_wasm_url, &ads_retrieval_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, ads_retrieval_url);
        EXPECT_EQ(requests[1].url, ads_retrieval_wasm_url);
        std::move(done_callback)({""});
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    dispatcher_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
