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

#include "services/auction_service/udf_fetcher/buyer_reporting_udf_fetch_manager.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/auction_service/udf_fetcher/auction_code_fetch_config.pb.h"
#include "services/common/code_fetch/periodic_code_fetcher.h"
#include "services/common/test/mocks.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

struct BuyerConfig {
  std::string pa_buyer_origin = "http://PABuyerOrigin.com";
  std::string pas_buyer_origin = "http://PASBuyerOrigin.com";
  std::string pa_buyer_udf_url = "foo.com";
  std::string pas_buyer_udf_url = "bar.com";
  bool enable_report_win_url_generation = true;
};

auction_service::SellerCodeFetchConfig GetTestSellerUdfConfig(
    const BuyerConfig& buyer_config) {
  auction_service::SellerCodeFetchConfig udf_config;
  udf_config.set_enable_report_win_url_generation(
      buyer_config.enable_report_win_url_generation);
  udf_config.mutable_buyer_report_win_js_urls()->try_emplace(
      buyer_config.pa_buyer_origin, buyer_config.pa_buyer_udf_url);
  udf_config.mutable_protected_app_signals_buyer_report_win_js_urls()
      ->try_emplace(buyer_config.pas_buyer_origin,
                    buyer_config.pas_buyer_udf_url);
  udf_config.set_url_fetch_timeout_ms(70000000);
  udf_config.set_url_fetch_period_ms(70000001);
  return udf_config;
}

auction_service::SellerCodeFetchConfig
GetTestSellerUdfConfigWithNoUrlsConfigured(const BuyerConfig& buyer_config) {
  auction_service::SellerCodeFetchConfig udf_config;
  udf_config.set_enable_report_win_url_generation(
      buyer_config.enable_report_win_url_generation);
  udf_config.set_url_fetch_timeout_ms(70000000);
  udf_config.set_url_fetch_period_ms(70000001);
  return udf_config;
}

TEST(BuyerReportingUdfFetchManagerTest,
     LoadsHttpFetcherResultIntoV8Dispatcher) {
  std::string expected_pa_version = "pa_http://PABuyerOrigin.com";
  std::string expected_pas_version = "pas_http://PASBuyerOrigin.com";
  std::vector<absl::StatusOr<std::string>> url_response = {
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE",
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE"};

  std::string expected_wrapped_code =
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE";

  BuyerConfig buyer_config;
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [expected_wrapped_code](const std::string& adtech_code_blob) {
        return expected_wrapped_code;
      };
  absl::BlockingCounter done(2);
  EXPECT_CALL(*curl_http_fetcher, FetchUrls)
      .WillOnce([&buyer_config, &url_response](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<std::string>>)&&>
                        done_callback) {
        EXPECT_EQ(buyer_config.pa_buyer_udf_url, requests[0].url);
        EXPECT_EQ(buyer_config.pas_buyer_udf_url, requests[1].url);
        std::move(done_callback)(url_response);
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillRepeatedly([&done, &expected_wrapped_code, &expected_pa_version,
                       &expected_pas_version](std::string_view version,
                                              absl::string_view js) {
        PS_VLOG(4) << "version:" << version
                   << " expected:" << expected_pa_version << " or "
                   << expected_pas_version << "\n";
        EXPECT_TRUE(version == expected_pa_version ||
                    version == expected_pas_version);
        EXPECT_EQ(js, expected_wrapped_code);
        done.DecrementCount();
        return absl::OkStatus();
      });

  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      std::move(buyer_code_wrapper), &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAppSignalsReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 1);
  EXPECT_EQ(protected_app_signals_code_blob_per_origin.size(), 1);
  EXPECT_EQ(
      protected_audience_code_blob_per_origin.at(buyer_config.pa_buyer_origin),
      expected_wrapped_code);
  EXPECT_EQ(protected_app_signals_code_blob_per_origin.at(
                buyer_config.pas_buyer_origin),
            expected_wrapped_code);
  code_fetcher_manager.End();
}

TEST(BuyerReportingUdfFetchManagerTest, NoCodeLoadedWhenFlagsTurnedOff) {
  BuyerConfig buyer_config = {.enable_report_win_url_generation = false};
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  MockV8Dispatcher dispatcher;
  std::string expected_wrapped_code =
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE";
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [expected_wrapped_code](const std::string& adtech_code_blob) {
        return expected_wrapped_code;
      };
  EXPECT_CALL(*curl_http_fetcher, FetchUrls).Times(0);
  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      std::move(buyer_code_wrapper), &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAppSignalsReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 0);
  EXPECT_EQ(protected_app_signals_code_blob_per_origin.size(), 0);
  code_fetcher_manager.End();
}

TEST(BuyerReportingUdfFetchManagerTest, NoCodeLoadedWhenNoFetchUrlConfigured) {
  std::string expected_wrapped_code =
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE";

  BuyerConfig buyer_config;
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfigWithNoUrlsConfigured(buyer_config);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [expected_wrapped_code](const std::string& adtech_code_blob) {
        return expected_wrapped_code;
      };
  EXPECT_CALL(*curl_http_fetcher, FetchUrls).Times(0);
  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      std::move(buyer_code_wrapper), &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAppSignalsReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 0);
  EXPECT_EQ(protected_app_signals_code_blob_per_origin.size(), 0);
  code_fetcher_manager.End();
}

TEST(BuyerReportingUdfFetchManagerTest, SkipsBuyerCodeUponFetchError) {
  std::string expected_pa_version = "pa_http://PABuyerOrigin.com";
  std::string expected_pas_version = "pas_http://PASBuyerOrigin.com";
  std::vector<absl::StatusOr<std::string>> url_response = {
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE",
      absl::Status(
          absl::StatusCode::kInternal,
          "Error fetching and loading one or more buyer's reportWin() udf.")};

  std::string expected_wrapped_code =
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE";

  BuyerConfig buyer_config;
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [expected_wrapped_code](const std::string& adtech_code_blob) {
        return expected_wrapped_code;
      };
  absl::Notification fetch_done;
  EXPECT_CALL(*curl_http_fetcher, FetchUrls)
      .WillOnce([&buyer_config, &url_response](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<std::string>>)&&>
                        done_callback) {
        EXPECT_EQ(buyer_config.pa_buyer_udf_url, requests[0].url);
        EXPECT_EQ(buyer_config.pas_buyer_udf_url, requests[1].url);
        std::move(done_callback)(url_response);
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&fetch_done, &expected_wrapped_code, &expected_pa_version,
                 &expected_pas_version](std::string_view version,
                                        absl::string_view js) {
        PS_VLOG(4) << "version:" << version
                   << " expected:" << expected_pa_version << " or "
                   << expected_pas_version << "\n";
        EXPECT_EQ(version, expected_pa_version);
        EXPECT_EQ(js, expected_wrapped_code);
        fetch_done.Notify();
        return absl::OkStatus();
      });

  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      std::move(buyer_code_wrapper), &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  fetch_done.WaitForNotification();
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAppSignalsReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 1);
  EXPECT_EQ(protected_app_signals_code_blob_per_origin.size(), 0);
  EXPECT_EQ(
      protected_audience_code_blob_per_origin.at(buyer_config.pa_buyer_origin),
      expected_wrapped_code);
  code_fetcher_manager.End();
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
