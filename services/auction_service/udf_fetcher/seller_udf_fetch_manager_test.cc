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

#include "services/auction_service/udf_fetcher/seller_udf_fetch_manager.h"

#include <include/gmock/gmock-matchers.h>

#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "services/auction_service/auction_constants.h"
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"
#include "services/common/data_fetch/version_util.h"
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

using auction_service::SellerCodeFetchConfig;
using blob_fetch::FetchMode;

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
using ::testing::StrEq;

struct TestSellerUdfConfig {
  std::string pa_buyer_origin = "http://PABuyerOrigin.com";
  std::string pas_buyer_origin = "http://PASBuyerOrigin.com";
  std::string seller_udf_url = "seller.com";
  std::string pa_buyer_udf_url = "foo.com/";
  std::string pas_buyer_udf_url = "bar.com/";
  bool enable_report_win_url_generation = true;
  FetchMode fetch_mode = blob_fetch::FETCH_MODE_URL;
  bool enable_report_result_url_generation = true;
  bool enable_seller_and_buyer_udf_isolation = false;
  std::string auction_js_bucket = "";
  std::string auction_js_bucket_default_blob = "";
  bool enable_private_aggregate_reporting = false;
};

class SellerUdfFetchManagerTest : public testing::Test {
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

auction_service::SellerCodeFetchConfig GetTestSellerUdfConfig(
    const TestSellerUdfConfig& test_seller_udf_config) {
  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(test_seller_udf_config.fetch_mode);
  udf_config.set_enable_report_win_url_generation(
      test_seller_udf_config.enable_report_win_url_generation);
  udf_config.set_enable_report_result_url_generation(
      test_seller_udf_config.enable_report_result_url_generation);
  udf_config.mutable_buyer_report_win_js_urls()->try_emplace(
      test_seller_udf_config.pa_buyer_origin,
      test_seller_udf_config.pa_buyer_udf_url);
  udf_config.mutable_protected_app_signals_buyer_report_win_js_urls()
      ->try_emplace(test_seller_udf_config.pas_buyer_origin,
                    test_seller_udf_config.pas_buyer_udf_url);
  udf_config.set_auction_js_url(test_seller_udf_config.seller_udf_url);
  udf_config.set_url_fetch_timeout_ms(70000000);
  udf_config.set_url_fetch_period_ms(70000001);
  udf_config.set_enable_seller_and_buyer_udf_isolation(
      test_seller_udf_config.enable_seller_and_buyer_udf_isolation);
  udf_config.set_auction_js_bucket(test_seller_udf_config.auction_js_bucket);
  udf_config.set_auction_js_bucket_default_blob(
      test_seller_udf_config.auction_js_bucket_default_blob);
  udf_config.set_enable_private_aggregate_reporting(
      test_seller_udf_config.enable_private_aggregate_reporting);
  return udf_config;
}

TEST_F(SellerUdfFetchManagerTest, FetchModeLocalTriesFileLoad) {
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);

  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_LOCAL);
  udf_config.set_enable_report_result_url_generation(true);
  const std::string bad_path = "error";
  const bool enable_protected_app_signals = true;

  udf_config.set_auction_js_path(bad_path);
  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_FALSE(load_status.ok());
  EXPECT_EQ(load_status.message(), absl::StrCat(kPathFailed, bad_path));
}

TEST_F(SellerUdfFetchManagerTest,
       FetchModeLocalSuccessWithSellerAndBuyerCodeIsolation) {
  std::string test_file =
      "services/auction_service/udf_fetcher/testScoreAds.js";
  std::string expected_buyer_udf = R"JS_CODE(
reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
}
)JS_CODE";

  absl::flat_hash_map<std::string, absl::StatusOr<std::string>>
      valid_response_headers;
  valid_response_headers.try_emplace(kUdfRequiredResponseHeader,
                                     absl::StatusOr<std::string>("true"));
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  bool enable_protected_app_signals = true;
  TestSellerUdfConfig test_udf_config = {
      .fetch_mode = blob_fetch::FETCH_MODE_LOCAL,
      .enable_seller_and_buyer_udf_isolation = true};
  SellerCodeFetchConfig udf_config = GetTestSellerUdfConfig(test_udf_config);
  udf_config.set_auction_js_path(test_file);
  udf_config.set_enable_seller_and_buyer_udf_isolation(true);
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*executor_, RunAfter).Times(1);
  EXPECT_CALL(*http_fetcher_, FetchUrlsWithMetadata)
      .Times(1)
      .WillOnce(
          [&test_udf_config, &expected_buyer_udf, &valid_response_headers](
              const std::vector<HTTPRequest>& requests, absl::Duration timeout,
              OnDoneFetchUrlsWithMetadata done_callback) {
            EXPECT_EQ(requests[0].url, test_udf_config.pa_buyer_udf_url);
            EXPECT_EQ(requests[1].url, test_udf_config.pas_buyer_udf_url);
            std::move(done_callback)({absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PABuyerOrigin.com"}),
                                      absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PASBuyerOrigin.com"})});
          });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .Times(3)
      .WillOnce([&expected_buyer_udf, &expected_pa_version,
                 &enable_protected_app_signals](std::string_view version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pa_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(expected_buyer_udf,
                                              enable_protected_app_signals)));
        return absl::OkStatus();
      })
      .WillOnce([&expected_buyer_udf, &expected_pas_version,
                 &enable_protected_app_signals](std::string_view version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pas_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(expected_buyer_udf,
                                              enable_protected_app_signals)));
        return absl::OkStatus();
      })
      .WillOnce([](std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, kScoreAdBlobVersion);
        // Verify that scoreAd, reportResult along with corresponding wrapper
        // functions are loaded
        EXPECT_NE(
            blob_data.find(
                "scoreAd(ad_metadata, bid, auction_config, scoring_signals, "
                "bid_metadata, directFromSellerSignals)"),
            std::string::npos);
        EXPECT_NE(
            blob_data.find("reportResult(auctionConfig, "
                           "sellerReportingSignals, directFromSellerSignals)"),
            std::string::npos);
        EXPECT_NE(blob_data.find("scoreAdEntryFunction"), std::string::npos);
        EXPECT_NE(blob_data.find("reportResultEntryFunction"),
                  std::string::npos);
        return absl::OkStatus();
      });

  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);

  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(SellerUdfFetchManagerTest,
       FetchModeBucketSucceedsLoadingWrappedBucketBlobs) {
  std::string fake_udf = "udf_data";
  std::string pa_buyer_origin = "pa_origin";
  std::string pas_buyer_origin = "pas_origin";
  std::string pa_reporting_udf_data = "pa_reporting_udf_data";
  std::string pas_reporting_udf_data = "pas_reporting_udf_data";
  const bool enable_protected_app_signals = true;

  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_BUCKET);
  udf_config.set_enable_report_win_url_generation(true);
  udf_config.set_enable_report_result_url_generation(true);
  udf_config.mutable_buyer_report_win_js_urls()->try_emplace(pa_buyer_origin,
                                                             "foo.com");
  udf_config.mutable_protected_app_signals_buyer_report_win_js_urls()
      ->try_emplace(pas_buyer_origin, "bar.com");
  udf_config.set_auction_js_bucket("js");
  udf_config.set_auction_js_bucket_default_blob("default");

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .Times(1)
      .WillOnce([&udf_config, &pa_buyer_origin, &pas_buyer_origin,
                 &pa_reporting_udf_data, &pas_reporting_udf_data](
                    const std::vector<HTTPRequest>& requests,
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
          [&udf_config](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      udf_config.auction_js_bucket());
            BlobMetadata md;
            md.set_bucket_name(udf_config.auction_js_bucket());
            md.set_blob_name(udf_config.auction_js_bucket_default_blob());

            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });

  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&udf_config, &fake_udf](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            const auto& async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            const auto& async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, udf_config.auction_js_bucket());
            EXPECT_EQ(async_blob_name,
                      udf_config.auction_js_bucket_default_blob());

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(fake_udf);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .WillOnce([&udf_config, &fake_udf, &pa_buyer_origin, &pas_buyer_origin,
                 &pa_reporting_udf_data, &pas_reporting_udf_data](
                    std::string_view version, absl::string_view blob_data) {
        absl::StatusOr<std::string> result =
            GetBucketBlobVersion(udf_config.auction_js_bucket(),
                                 udf_config.auction_js_bucket_default_blob());
        EXPECT_TRUE(result.ok());
        EXPECT_EQ(version, *result);
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

  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(SellerUdfFetchManagerTest,
       FetchModeBucketSucceedsWithSellerAndBuyerUdfIsolation) {
  std::string expected_seller_udf = R"JS_CODE(
scoreAd = function(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
}
reportResult = function(auctionConfig, sellerReportingSignals, directFromSellerSignals){
}
)JS_CODE";
  std::string expected_buyer_udf = R"JS_CODE(
reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
}
)JS_CODE";
  absl::flat_hash_map<std::string, absl::StatusOr<std::string>>
      valid_response_headers;
  valid_response_headers.try_emplace(kUdfRequiredResponseHeader,
                                     absl::StatusOr<std::string>("true"));
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  bool enable_protected_app_signals = true;
  TestSellerUdfConfig test_udf_config = {
      .fetch_mode = blob_fetch::FETCH_MODE_BUCKET,
      .enable_seller_and_buyer_udf_isolation = true,
      .auction_js_bucket = "js",
      .auction_js_bucket_default_blob = "default"};
  SellerCodeFetchConfig udf_config = GetTestSellerUdfConfig(test_udf_config);

  EXPECT_CALL(*blob_storage_client_, Init).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*blob_storage_client_, Run).WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrlsWithMetadata)
      .Times(1)
      .WillOnce(
          [&test_udf_config, &expected_buyer_udf, &valid_response_headers](
              const std::vector<HTTPRequest>& requests, absl::Duration timeout,
              OnDoneFetchUrlsWithMetadata done_callback) {
            EXPECT_EQ(requests[0].url, test_udf_config.pa_buyer_udf_url);
            EXPECT_EQ(requests[1].url, test_udf_config.pas_buyer_udf_url);
            std::move(done_callback)({absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PABuyerOrigin.com"}),
                                      absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PASBuyerOrigin.com"})});
          });

  EXPECT_CALL(*blob_storage_client_, ListBlobsMetadata)
      .WillOnce(
          [&udf_config](
              AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
                  context) {
            EXPECT_EQ(context.request->blob_metadata().bucket_name(),
                      udf_config.auction_js_bucket());
            BlobMetadata md;
            md.set_bucket_name(udf_config.auction_js_bucket());
            md.set_blob_name(udf_config.auction_js_bucket_default_blob());

            context.response = std::make_shared<ListBlobsMetadataResponse>();
            context.response->mutable_blob_metadatas()->Add(std::move(md));
            context.Finish(SuccessExecutionResult());
            return absl::OkStatus();
          });

  EXPECT_CALL(*blob_storage_client_, GetBlob)
      .WillOnce(
          [&udf_config, &expected_seller_udf](
              AsyncContext<GetBlobRequest, GetBlobResponse> async_context) {
            const auto& async_bucket_name =
                async_context.request->blob_metadata().bucket_name();
            const auto& async_blob_name =
                async_context.request->blob_metadata().blob_name();
            EXPECT_EQ(async_bucket_name, udf_config.auction_js_bucket());
            EXPECT_EQ(async_blob_name,
                      udf_config.auction_js_bucket_default_blob());

            async_context.response = std::make_shared<GetBlobResponse>();
            async_context.response->mutable_blob()->set_data(
                expected_seller_udf);
            async_context.result = SuccessExecutionResult();
            async_context.Finish();

            return absl::OkStatus();
          });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .Times(3)
      .WillOnce([&expected_buyer_udf, &expected_pa_version,
                 &enable_protected_app_signals](std::string_view version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pa_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(expected_buyer_udf,
                                              enable_protected_app_signals)));
        return absl::OkStatus();
      })
      .WillOnce([&expected_buyer_udf, &expected_pas_version,
                 &enable_protected_app_signals](std::string_view version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pas_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(expected_buyer_udf,
                                              enable_protected_app_signals)));
        return absl::OkStatus();
      })
      .WillOnce([&udf_config, &expected_seller_udf](
                    std::string_view version, absl::string_view blob_data) {
        absl::StatusOr<std::string> result =
            GetBucketBlobVersion(udf_config.auction_js_bucket(),
                                 udf_config.auction_js_bucket_default_blob());
        EXPECT_TRUE(result.ok());
        EXPECT_EQ(version, *result);
        EXPECT_EQ(blob_data,
                  GetSellerWrappedCode(
                      expected_seller_udf,
                      udf_config.enable_report_result_url_generation(),
                      udf_config.enable_private_aggregate_reporting()));
        return absl::OkStatus();
      });

  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(SellerUdfFetchManagerTest,
       ReportWinWrappingSuccessWithPrivateAggregationEnabled) {
  std::string expected_seller_udf = R"JS_CODE(
scoreAd = function(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
}
reportResult = function(auctionConfig, sellerReportingSignals, directFromSellerSignals){
}
)JS_CODE";
  std::string expected_buyer_udf = R"JS_CODE(
reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
}
)JS_CODE";
  absl::flat_hash_map<std::string, absl::StatusOr<std::string>>
      valid_response_headers;
  valid_response_headers.try_emplace(kUdfRequiredResponseHeader,
                                     absl::StatusOr<std::string>("true"));
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  bool enable_protected_app_signals = false;
  bool enable_private_aggregate_reporting = true;
  TestSellerUdfConfig test_udf_config = {
      .enable_report_result_url_generation = false,
      .enable_seller_and_buyer_udf_isolation = true,
      .enable_private_aggregate_reporting = enable_private_aggregate_reporting};
  SellerCodeFetchConfig udf_config = GetTestSellerUdfConfig(test_udf_config);
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrlsWithMetadata)
      .WillOnce(
          [&test_udf_config, &expected_buyer_udf, &valid_response_headers](
              const std::vector<HTTPRequest>& requests, absl::Duration timeout,
              OnDoneFetchUrlsWithMetadata done_callback) {
            EXPECT_EQ(requests[0].url, test_udf_config.pa_buyer_udf_url);
            EXPECT_EQ(requests[1].url, test_udf_config.pas_buyer_udf_url);

            std::move(done_callback)({absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PABuyerOrigin.com"}),
                                      absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PASBuyerOrigin.com"})});
          });
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&udf_config, &expected_seller_udf](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, udf_config.auction_js_url());
        std::move(done_callback)({expected_seller_udf});
      });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .Times(3)
      .WillOnce([&expected_buyer_udf, &expected_pa_version,
                 &enable_protected_app_signals,
                 enable_private_aggregate_reporting](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pa_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(
                        expected_buyer_udf, enable_protected_app_signals,
                        enable_private_aggregate_reporting)));
        return absl::OkStatus();
      })
      .WillOnce([&expected_buyer_udf, &expected_pas_version,
                 &enable_protected_app_signals,
                 enable_private_aggregate_reporting](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pas_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(
                        expected_buyer_udf, enable_protected_app_signals,
                        enable_private_aggregate_reporting)));
        return absl::OkStatus();
      })
      .WillOnce([&udf_config, &expected_seller_udf](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, kScoreAdBlobVersion);
        EXPECT_EQ(blob_data,
                  GetSellerWrappedCode(
                      expected_seller_udf,
                      udf_config.enable_report_result_url_generation(),
                      udf_config.enable_private_aggregate_reporting()));
        return absl::OkStatus();
      });

  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(SellerUdfFetchManagerTest,
       FetchModeUrlSucceedsWithSellerAndBuyerCodeIsolation) {
  std::string expected_seller_udf = R"JS_CODE(
scoreAd = function(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
}
reportResult = function(auctionConfig, sellerReportingSignals, directFromSellerSignals){
}
)JS_CODE";
  std::string expected_buyer_udf = R"JS_CODE(
reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
}
)JS_CODE";
  absl::flat_hash_map<std::string, absl::StatusOr<std::string>>
      valid_response_headers;
  valid_response_headers.try_emplace(kUdfRequiredResponseHeader,
                                     absl::StatusOr<std::string>("true"));
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  bool enable_protected_app_signals = true;
  TestSellerUdfConfig test_udf_config = {
      .enable_seller_and_buyer_udf_isolation = true};
  SellerCodeFetchConfig udf_config = GetTestSellerUdfConfig(test_udf_config);
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrlsWithMetadata)
      .WillOnce(
          [&test_udf_config, &expected_buyer_udf, &valid_response_headers](
              const std::vector<HTTPRequest>& requests, absl::Duration timeout,
              OnDoneFetchUrlsWithMetadata done_callback) {
            EXPECT_EQ(requests[0].url, test_udf_config.pa_buyer_udf_url);
            EXPECT_EQ(requests[1].url, test_udf_config.pas_buyer_udf_url);

            std::move(done_callback)({absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PABuyerOrigin.com"}),
                                      absl::StatusOr<HTTPResponse>(HTTPResponse{
                                          .body = expected_buyer_udf,
                                          .headers = valid_response_headers,
                                          .final_url = "PASBuyerOrigin.com"})});
          });

  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .WillOnce([&udf_config, &expected_seller_udf](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, udf_config.auction_js_url());
        std::move(done_callback)({expected_seller_udf});
      });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .Times(3)
      .WillOnce([&expected_buyer_udf, &expected_pa_version,
                 &enable_protected_app_signals](std::string_view version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pa_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(expected_buyer_udf,
                                              enable_protected_app_signals)));
        return absl::OkStatus();
      })
      .WillOnce([&expected_buyer_udf, &expected_pas_version,
                 &enable_protected_app_signals](std::string_view version,
                                                absl::string_view blob_data) {
        EXPECT_EQ(version, expected_pas_version);

        EXPECT_THAT(blob_data,
                    StrEq(GetBuyerWrappedCode(expected_buyer_udf,
                                              enable_protected_app_signals)));
        return absl::OkStatus();
      })
      .WillOnce([&udf_config, &expected_seller_udf](
                    std::string_view version, absl::string_view blob_data) {
        EXPECT_EQ(version, kScoreAdBlobVersion);
        EXPECT_EQ(blob_data,
                  GetSellerWrappedCode(
                      expected_seller_udf,
                      udf_config.enable_report_result_url_generation(),
                      udf_config.enable_private_aggregate_reporting()));
        return absl::OkStatus();
      });

  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

TEST_F(SellerUdfFetchManagerTest, FetchModeUrlSucceedsLoadingWrappedUrlBlobs) {
  std::string fake_udf = "udf_data";
  std::string pa_buyer_origin = "pa_origin";
  std::string pas_buyer_origin = "pas_origin";
  std::string pa_reporting_udf_data = "pa_reporting_udf_data";
  std::string pas_reporting_udf_data = "pas_reporting_udf_data";
  const bool enable_protected_app_signals = true;

  SellerCodeFetchConfig udf_config;
  udf_config.set_fetch_mode(blob_fetch::FETCH_MODE_URL);
  udf_config.set_enable_report_win_url_generation(true);
  udf_config.set_enable_report_result_url_generation(true);
  udf_config.mutable_buyer_report_win_js_urls()->try_emplace(pa_buyer_origin,
                                                             "foo.com/");
  udf_config.mutable_protected_app_signals_buyer_report_win_js_urls()
      ->try_emplace(pas_buyer_origin, "bar.com/");
  udf_config.set_auction_js_url("auction.com");
  udf_config.set_url_fetch_timeout_ms(70000000);
  udf_config.set_url_fetch_period_ms(70000001);
  EXPECT_CALL(*blob_storage_client_, Init).Times(0);
  EXPECT_CALL(*blob_storage_client_, Run).Times(0);
  EXPECT_CALL(*executor_, RunAfter).Times(2);
  EXPECT_CALL(*http_fetcher_, FetchUrls)
      .Times(2)
      .WillOnce([&udf_config, &pa_buyer_origin, &pas_buyer_origin,
                 &pa_reporting_udf_data, &pas_reporting_udf_data](
                    const std::vector<HTTPRequest>& requests,
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
      .WillOnce([&udf_config, &fake_udf](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout, OnDoneFetchUrls done_callback) {
        EXPECT_EQ(requests[0].url, udf_config.auction_js_url());
        std::move(done_callback)({fake_udf});
      });

  EXPECT_CALL(*dispatcher_, LoadSync)
      .WillOnce([&udf_config, &fake_udf, &pa_buyer_origin, &pas_buyer_origin,
                 &pa_reporting_udf_data, &pas_reporting_udf_data](
                    std::string_view version, absl::string_view blob_data) {
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

  SellerUdfFetchManager udf_fetcher(std::move(blob_storage_client_),
                                    executor_.get(), http_fetcher_.get(),
                                    http_fetcher_.get(), dispatcher_.get(),
                                    udf_config, enable_protected_app_signals);
  absl::Status load_status = udf_fetcher.Init();
  EXPECT_TRUE(load_status.ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
