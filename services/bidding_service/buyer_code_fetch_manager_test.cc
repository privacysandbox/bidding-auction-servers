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

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"
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

class BuyerCodeFetchManagerTest : public testing::Test {
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

TEST_F(BuyerCodeFetchManagerTest, LocalModeTriesFileLoadJs) {
  const std::string pa_js_path = "pa_js";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_LOCAL);
  udf_config.set_bidding_js_path(pa_js_path);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    false /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(), absl::StrCat(kPathFailed, pa_js_path));
}

TEST_F(BuyerCodeFetchManagerTest, BucketModeFailsForNoPABucket) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyBucketName, kProtectedAuctionJsId));
}

TEST_F(BuyerCodeFetchManagerTest, BucketModeFetchesJsForPA) {
  const std::string pa_js_bucket = "pa_js";
  const std::string pa_js_object = "pa_js_test";
  const std::string pa_js_data = "pa_js_data";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_auction_bidding_js_bucket(pa_js_bucket);
  udf_config.set_protected_auction_bidding_js_bucket_default_blob(pa_js_object);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&pa_js_bucket, &pa_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pa_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(pa_js_bucket);
            md.set_blob_name(pa_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&pa_js_bucket, &pa_js_object, &pa_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pa_js_bucket);
            EXPECT_EQ(async_blob_name, pa_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pa_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });
  EXPECT_CALL(*loader_, LoadSync)
      .WillOnce([&pa_js_object, &pa_js_data](std::string_view version,
                                             absl::string_view blob_data) {
        EXPECT_EQ(version, pa_js_object);
        EXPECT_EQ(blob_data, GetBuyerWrappedCode(pa_js_data, {}));
        return absl::OkStatus();
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    false /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(BuyerCodeFetchManagerTest, BucketModeFailsForNoPASBucket) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_ads_retrieval_js_bucket("ads_js");  // should be ignored
  udf_config.set_ads_retrieval_bucket_default_blob(
      "ads_js_test");  // should be ignored

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata).Times(0);

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyBucketName, kProtectedAppSignalsJsId));
}

TEST_F(BuyerCodeFetchManagerTest, BucketModeFailsForNoAdsRetrievalBucket) {
  const std::string pas_js_bucket = "pas_js";
  const std::string pas_js_object = "pas_js_test";
  const std::string pas_js_data = "pas_js_data";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_app_signals_bidding_js_bucket(pas_js_bucket);
  udf_config.set_protected_app_signals_bidding_js_bucket_default_blob(
      pas_js_object);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&pas_js_bucket, &pas_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pas_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(pas_js_bucket);
            md.set_blob_name(pas_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&pas_js_bucket, &pas_js_object, &pas_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pas_js_bucket);
            EXPECT_EQ(async_blob_name, pas_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pas_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });
  BuyerCodeWrapperConfig pas_js_wrapper_config = {
      .auction_type = AuctionType::kProtectedAppSignals,
      .auction_specific_setup = kEncodedProtectedAppSignalsHandler};
  EXPECT_CALL(*loader_, LoadSync)
      .WillOnce([&pas_js_object, &pas_js_data, &pas_js_wrapper_config](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, pas_js_object);
        EXPECT_EQ(blob_data,
                  GetBuyerWrappedCode(pas_js_data, pas_js_wrapper_config));
        return absl::OkStatus();
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyBucketName, kAdsRetrievalJsId));
}

TEST_F(BuyerCodeFetchManagerTest, BucketModeFetchesJsForPAS) {
  const std::string pas_js_bucket = "pas_js";
  const std::string pas_js_object = "pas_js_test";
  const std::string pas_js_data = "pas_js_data";
  const std::string ads_js_bucket = "ads_js";
  const std::string ads_js_object = "ads_js_test";
  const std::string ads_js_data = "ads_js_data";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_app_signals_bidding_js_bucket(pas_js_bucket);
  udf_config.set_protected_app_signals_bidding_js_bucket_default_blob(
      pas_js_object);
  udf_config.set_ads_retrieval_js_bucket(ads_js_bucket);
  udf_config.set_ads_retrieval_bucket_default_blob(ads_js_object);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&pas_js_bucket, &pas_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pas_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(pas_js_bucket);
            md.set_blob_name(pas_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          })
      .WillOnce(
          [&ads_js_bucket, &ads_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      ads_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(ads_js_bucket);
            md.set_blob_name(ads_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&pas_js_bucket, &pas_js_object, &pas_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pas_js_bucket);
            EXPECT_EQ(async_blob_name, pas_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pas_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          })
      .WillOnce(
          [&ads_js_bucket, &ads_js_object, &ads_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, ads_js_bucket);
            EXPECT_EQ(async_blob_name, ads_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(ads_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });
  BuyerCodeWrapperConfig pas_js_wrapper_config = {
      .auction_type = AuctionType::kProtectedAppSignals,
      .auction_specific_setup = kEncodedProtectedAppSignalsHandler};
  EXPECT_CALL(*loader_, LoadSync)
      .WillOnce([&pas_js_object, &pas_js_data, &pas_js_wrapper_config](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, pas_js_object);
        EXPECT_EQ(blob_data,
                  GetBuyerWrappedCode(pas_js_data, pas_js_wrapper_config));
        return absl::OkStatus();
      })
      .WillOnce([&ads_js_object, &ads_js_data](std::string_view version,
                                               absl::string_view blob_data) {
        EXPECT_EQ(version, ads_js_object);
        EXPECT_EQ(blob_data, GetProtectedAppSignalsGenericBuyerWrappedCode(
                                 ads_js_data, kUnusedWasmBlob,
                                 kPrepareDataForAdRetrievalHandler,
                                 kPrepareDataForAdRetrievalArgs));
        return absl::OkStatus();
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(BuyerCodeFetchManagerTest, BucketModeFetchesJsForPAAndPAS) {
  const std::string pa_js_bucket = "pa_js";
  const std::string pa_js_object = "pa_js_test";
  const std::string pa_js_data = "pa_js_data";
  const std::string pas_js_bucket = "pas_js";
  const std::string pas_js_object = "pas_js_test";
  const std::string pas_js_data = "pas_js_data";
  const std::string ads_js_bucket = "ads_js";
  const std::string ads_js_object = "ads_js_test";
  const std::string ads_js_data = "ads_js_data";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_url_fetch_period_ms(1);
  udf_config.set_protected_auction_bidding_js_bucket(pa_js_bucket);
  udf_config.set_protected_auction_bidding_js_bucket_default_blob(pa_js_object);
  udf_config.set_protected_app_signals_bidding_js_bucket(pas_js_bucket);
  udf_config.set_protected_app_signals_bidding_js_bucket_default_blob(
      pas_js_object);
  udf_config.set_ads_retrieval_js_bucket(ads_js_bucket);
  udf_config.set_ads_retrieval_bucket_default_blob(ads_js_object);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&pa_js_bucket, &pa_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pa_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(pa_js_bucket);
            md.set_blob_name(pa_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          })
      .WillOnce(
          [&pas_js_bucket, &pas_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      pas_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(pas_js_bucket);
            md.set_blob_name(pas_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          })
      .WillOnce(
          [&ads_js_bucket, &ads_js_object](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      ads_js_bucket);
            BlobMetadata md;
            md.set_bucket_name(ads_js_bucket);
            md.set_blob_name(ads_js_object);
            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });
  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&pa_js_bucket, &pa_js_object, &pa_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pa_js_bucket);
            EXPECT_EQ(async_blob_name, pa_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pa_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          })
      .WillOnce(
          [&pas_js_bucket, &pas_js_object, &pas_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, pas_js_bucket);
            EXPECT_EQ(async_blob_name, pas_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(pas_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          })
      .WillOnce(
          [&ads_js_bucket, &ads_js_object, &ads_js_data](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            auto async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            auto async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, ads_js_bucket);
            EXPECT_EQ(async_blob_name, ads_js_object);
            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(ads_js_data);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();
            return absl::OkStatus();
          });
  BuyerCodeWrapperConfig pa_js_wrapper_config = {};
  BuyerCodeWrapperConfig pas_js_wrapper_config = {
      .auction_type = AuctionType::kProtectedAppSignals,
      .auction_specific_setup = kEncodedProtectedAppSignalsHandler};
  EXPECT_CALL(*loader_, LoadSync)
      .WillOnce([&pa_js_object, &pa_js_data, &pa_js_wrapper_config](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, pa_js_object);
        EXPECT_EQ(blob_data,
                  GetBuyerWrappedCode(pa_js_data, pa_js_wrapper_config));
        return absl::OkStatus();
      })
      .WillOnce([&pas_js_object, &pas_js_data, &pas_js_wrapper_config](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, pas_js_object);
        EXPECT_EQ(blob_data,
                  GetBuyerWrappedCode(pas_js_data, pas_js_wrapper_config));
        return absl::OkStatus();
      })
      .WillOnce([&ads_js_object, &ads_js_data](std::string_view version,
                                               absl::string_view blob_data) {
        EXPECT_EQ(version, ads_js_object);
        EXPECT_EQ(blob_data, GetProtectedAppSignalsGenericBuyerWrappedCode(
                                 ads_js_data, kUnusedWasmBlob,
                                 kPrepareDataForAdRetrievalHandler,
                                 kPrepareDataForAdRetrievalArgs));
        return absl::OkStatus();
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(BuyerCodeFetchManagerTest, UrlModeFailsForNoPAUrl) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_bidding_wasm_helper_url("pa_wasm");  // should be ignored

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*http_fetcher_, FetchUrls).Times(0);

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    false /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyUrl, kProtectedAuctionJsUrlId));
}

TEST_F(BuyerCodeFetchManagerTest, UrlModeFetchesJsForPA) {
  const std::string pa_js_url = "pa_js";
  const std::string pa_wasm_url = "pa_wasm";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_bidding_js_url(pa_js_url);
  udf_config.set_bidding_wasm_helper_url(pa_wasm_url);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*loader_, LoadSync)
      .WillRepeatedly(
          [](std::string_view version, absl::string_view blob_data) {
            return absl::OkStatus();
          });
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&pa_js_url, &pa_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, pa_js_url);
        EXPECT_EQ(requests[1].url, pa_wasm_url);
        std::move(done_callback)({""});
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    false /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(BuyerCodeFetchManagerTest, UrlModeFailsForNoPASUrl) {
  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_prepare_data_for_ads_retrieval_js_url(
      "ads_js");  // should be ignored
  udf_config.set_prepare_data_for_ads_retrieval_wasm_helper_url(
      "ads_wasm");  // should be ignored

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*http_fetcher_, FetchUrls).Times(0);

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyUrl, kProtectedAppSignalsJsUrlId));
}

TEST_F(BuyerCodeFetchManagerTest, UrlModeFailsForNoAdsRetrievalUrl) {
  const std::string pas_js_url = "pas_js";
  const std::string pas_wasm_url = "pas_wasm";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_protected_app_signals_bidding_js_url(pas_js_url);
  udf_config.set_protected_app_signals_bidding_wasm_helper_url(pas_wasm_url);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*loader_, LoadSync)
      .WillRepeatedly(
          [](std::string_view version, absl::string_view blob_data) {
            return absl::OkStatus();
          });
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&pas_js_url, &pas_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, pas_js_url);
        EXPECT_EQ(requests[1].url, pas_wasm_url);
        std::move(done_callback)({""});
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  ASSERT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(),
            absl::StrCat(kEmptyUrl, kAdsRetrievalJsUrlId));
}

TEST_F(BuyerCodeFetchManagerTest, UrlModeFetchesJsForPAS) {
  const std::string pas_js_url = "pas_js";
  const std::string pas_wasm_url = "pas_wasm";
  const std::string ads_js_url = "ads_js";
  const std::string ads_wasm_url = "ads_wasm";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_protected_app_signals_bidding_js_url(pas_js_url);
  udf_config.set_protected_app_signals_bidding_wasm_helper_url(pas_wasm_url);
  udf_config.set_prepare_data_for_ads_retrieval_js_url(ads_js_url);
  udf_config.set_prepare_data_for_ads_retrieval_wasm_helper_url(ads_wasm_url);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*loader_, LoadSync)
      .WillRepeatedly(
          [](std::string_view version, absl::string_view blob_data) {
            return absl::OkStatus();
          });
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&pas_js_url, &pas_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, pas_js_url);
        EXPECT_EQ(requests[1].url, pas_wasm_url);
        std::move(done_callback)({""});
      })
      .WillOnce([&ads_js_url, &ads_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, ads_js_url);
        EXPECT_EQ(requests[1].url, ads_wasm_url);
        std::move(done_callback)({""});
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    false /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(BuyerCodeFetchManagerTest, UrlModeFetchesJsForPAAndPAS) {
  const std::string pa_js_url = "pa_js";
  const std::string pa_wasm_url = "pa_wasm";
  const std::string pas_js_url = "pas_js";
  const std::string pas_wasm_url = "pas_wasm";
  const std::string ads_js_url = "ads_js";
  const std::string ads_wasm_url = "ads_wasm";

  BuyerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_url_fetch_period_ms(200000);
  udf_config.set_url_fetch_timeout_ms(100000);
  udf_config.set_bidding_js_url(pa_js_url);
  udf_config.set_bidding_wasm_helper_url(pa_wasm_url);
  udf_config.set_protected_app_signals_bidding_js_url(pas_js_url);
  udf_config.set_protected_app_signals_bidding_wasm_helper_url(pas_wasm_url);
  udf_config.set_prepare_data_for_ads_retrieval_js_url(ads_js_url);
  udf_config.set_prepare_data_for_ads_retrieval_wasm_helper_url(ads_wasm_url);

  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*loader_, LoadSync)
      .WillRepeatedly(
          [](std::string_view version, absl::string_view blob_data) {
            return absl::OkStatus();
          });
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&pa_js_url, &pa_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, pa_js_url);
        EXPECT_EQ(requests[1].url, pa_wasm_url);
        std::move(done_callback)({""});
      })
      .WillOnce([&pas_js_url, &pas_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, pas_js_url);
        EXPECT_EQ(requests[1].url, pas_wasm_url);
        std::move(done_callback)({""});
      })
      .WillOnce([&ads_js_url, &ads_wasm_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests.size(), 2);
        EXPECT_EQ(requests[0].url, ads_js_url);
        EXPECT_EQ(requests[1].url, ads_wasm_url);
        std::move(done_callback)({""});
      });

  BuyerCodeFetchManager udf_fetcher(executor_.get(), http_fetcher_.get(),
                                    loader_.get(),
                                    std::move(blob_storage_client_), udf_config,
                                    true /*enable_protected_audience*/,
                                    true /*enable_protected_app_signals*/);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
