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
#include "services/common/data_fetch/periodic_code_fetcher.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

struct BuyerConfig {
  std::string pa_buyer_origin = "http://PABuyerOrigin.com";
  std::string pas_buyer_origin = "http://PASBuyerOrigin.com";
  std::string pa_buyer_udf_url = "PABuyerOrigin.com/foo";
  std::string pas_buyer_udf_url = "PASBuyerOrigin.com/bar";
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

absl::flat_hash_map<std::string, absl::StatusOr<std::string>>
GetValidResponseHeaders() {
  absl::flat_hash_map<std::string, absl::StatusOr<std::string>> output;
  output.try_emplace(kUdfRequiredResponseHeader,
                     absl::StatusOr<std::string>("true"));
  return output;
}

absl::StatusOr<HTTPResponse> GetValidUdfResponse(std::string body,
                                                 std::string url) {
  return absl::StatusOr<HTTPResponse>(HTTPResponse{
      .body = std::move(body),
      .headers = GetValidResponseHeaders(),
      .final_url = std::move(url),
  });
}

class BuyerReportingUdfFetchManagerTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(BuyerReportingUdfFetchManagerTest,
       LoadsHttpFetcherResultIntoV8Dispatcher) {
  BuyerConfig buyer_config;
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  absl::StatusOr<HTTPResponse> expected_pa_response = GetValidUdfResponse(
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){})JS_CODE",
      buyer_config.pa_buyer_udf_url);
  absl::StatusOr<HTTPResponse> expected_pas_response = GetValidUdfResponse(
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){})JS_CODE",
      buyer_config.pas_buyer_udf_url);

  std::string expected_wrapped_code =
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){})JS_CODE";

  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  udf_config.set_test_mode(false);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [expected_wrapped_code](const std::string& adtech_code_blob) {
        return expected_wrapped_code;
      };
  absl::BlockingCounter done(2);
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce([&buyer_config, &expected_pa_response, &expected_pas_response](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<HTTPResponse>>)&&>
                        done_callback) {
        EXPECT_EQ(buyer_config.pa_buyer_udf_url, requests[0].url);
        EXPECT_EQ(buyer_config.pas_buyer_udf_url, requests[1].url);
        EXPECT_TRUE(requests[0].redirect_config.strict_http);
        EXPECT_TRUE(requests[1].redirect_config.strict_http);
        EXPECT_TRUE(requests[0].redirect_config.get_redirect_url);
        EXPECT_TRUE(requests[1].redirect_config.get_redirect_url);
        ASSERT_EQ(requests[0].include_headers.size(), 1);
        EXPECT_EQ(requests[0].include_headers[0], kUdfRequiredResponseHeader);
        ASSERT_EQ(requests[1].include_headers.size(), 1);
        EXPECT_EQ(requests[1].include_headers[0], kUdfRequiredResponseHeader);
        std::move(done_callback)({expected_pa_response, expected_pas_response});
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillRepeatedly([&done, &expected_wrapped_code, &expected_pa_version,
                       &expected_pas_version](std::string_view version,
                                              absl::string_view js) {
        std::cerr << "version:" << version
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
}

TEST_F(BuyerReportingUdfFetchManagerTest,
       LoadsHttpFetcherResultIntoV8DispatcherWithTestMode) {
  BuyerConfig buyer_config;
  buyer_config.pa_buyer_udf_url = "foo.com";
  buyer_config.pas_buyer_udf_url = "bar.com";
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  absl::StatusOr<HTTPResponse> expected_pa_response = GetValidUdfResponse(
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){})JS_CODE",
      buyer_config.pa_buyer_udf_url);
  absl::StatusOr<HTTPResponse> expected_pas_response = GetValidUdfResponse(
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){})JS_CODE",
      buyer_config.pas_buyer_udf_url);

  std::string expected_wrapped_code =
      R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){})JS_CODE";

  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  udf_config.set_test_mode(true);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [expected_wrapped_code](const std::string& adtech_code_blob) {
        return expected_wrapped_code;
      };
  absl::BlockingCounter done(2);
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce([&buyer_config, &expected_pa_response, &expected_pas_response](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<HTTPResponse>>)&&>
                        done_callback) {
        EXPECT_EQ(buyer_config.pa_buyer_udf_url, requests[0].url);
        EXPECT_EQ(buyer_config.pas_buyer_udf_url, requests[1].url);
        std::move(done_callback)({expected_pa_response, expected_pas_response});
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillRepeatedly([&done, &expected_wrapped_code, &expected_pa_version,
                       &expected_pas_version](std::string_view version,
                                              absl::string_view js) {
        std::cerr << "version:" << version
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
}

TEST_F(BuyerReportingUdfFetchManagerTest, StripsUrlBeforeFetching) {
  BuyerConfig buyer_config;
  std::string expected_pa_url = "foo.com/foo.js";
  buyer_config.pa_buyer_udf_url = "foo.com/foo.js?foo=bar#reportPA";
  buyer_config.pas_buyer_udf_url = "";
  absl::StatusOr<HTTPResponse> expected_pa_response =
      GetValidUdfResponse("{}", buyer_config.pa_buyer_udf_url);

  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  udf_config.set_test_mode(false);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  WrapSingleCodeBlobForDispatch buyer_code_wrapper =
      [](const std::string& adtech_code_blob) { return adtech_code_blob; };
  absl::BlockingCounter done(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce([&expected_pa_url, &expected_pa_response](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<HTTPResponse>>)&&>
                        done_callback) {
        EXPECT_EQ(expected_pa_url, requests[0].url);
        std::move(done_callback)({expected_pa_response});
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&done](std::string_view version, absl::string_view js) {
        done.DecrementCount();
        return absl::OkStatus();
      });

  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      std::move(buyer_code_wrapper), &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
}

TEST_F(BuyerReportingUdfFetchManagerTest, NoCodeLoadedWhenFlagsTurnedOff) {
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
}

TEST_F(BuyerReportingUdfFetchManagerTest,
       NoCodeLoadedWhenNoFetchUrlConfigured) {
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
}

TEST_F(BuyerReportingUdfFetchManagerTest, SkipsBuyerCodeUponFetchError) {
  std::string expected_pa_version = "pa_PABuyerOrigin.com";
  std::string expected_pas_version = "pas_PASBuyerOrigin.com";
  std::vector<absl::StatusOr<HTTPResponse>> url_response = {
      absl::StatusOr<HTTPResponse>(
          HTTPResponse{.body = R"JS_CODE(function reportWin(){})JS_CODE",
                       .headers = GetValidResponseHeaders(),
                       .final_url = "http://PABuyerOrigin.com"}),
      absl::Status(
          absl::StatusCode::kInternal,
          "Error fetching and loading one or more buyer's reportWin() udf.")};
  std::string expected_wrapped_code = R"JS_CODE(function reportWin(){})JS_CODE";

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
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce(
          [&url_response](
              const std::vector<HTTPRequest>& requests, absl::Duration timeout,
              absl::AnyInvocable<void(
                  std::vector<absl::StatusOr<HTTPResponse>>)&&>
                  done_callback) { std::move(done_callback)(url_response); });

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
}

TEST_F(BuyerReportingUdfFetchManagerTest, SkipsBuyerCodeForEmptyResponse) {
  BuyerConfig buyer_config;
  absl::StatusOr<HTTPResponse> expected_pa_response =
      GetValidUdfResponse("", buyer_config.pa_buyer_origin);
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  absl::Notification fetch_done;
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce([&expected_pa_response, &fetch_done](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<HTTPResponse>>)&&>
                        done_callback) {
        std::move(done_callback)({expected_pa_response});
        fetch_done.Notify();
      });

  EXPECT_CALL(dispatcher, LoadSync).Times(0);

  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      [](const std::string& adtech_code_blob) { return adtech_code_blob; },
      &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  fetch_done.WaitForNotification();
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 0);
}

TEST_F(BuyerReportingUdfFetchManagerTest,
       SkipsBuyerCodeForMissingResponseHeader) {
  BuyerConfig buyer_config;
  absl::StatusOr<HTTPResponse> expected_pa_response = GetValidUdfResponse(
      "function reportWin(){}", buyer_config.pa_buyer_origin);
  expected_pa_response->headers.erase(kUdfRequiredResponseHeader);
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  absl::Notification fetch_done;
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce([&expected_pa_response, &fetch_done](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<HTTPResponse>>)&&>
                        done_callback) {
        std::move(done_callback)({expected_pa_response});
        fetch_done.Notify();
      });

  EXPECT_CALL(dispatcher, LoadSync).Times(0);

  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      [](const std::string& adtech_code_blob) { return adtech_code_blob; },
      &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  fetch_done.WaitForNotification();
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 0);
}

TEST_F(BuyerReportingUdfFetchManagerTest,
       SkipsBuyerCodeForIncorrectResponseHeader) {
  BuyerConfig buyer_config;
  absl::StatusOr<HTTPResponse> expected_pa_response = GetValidUdfResponse(
      "function reportWin(){}", buyer_config.pa_buyer_origin);
  expected_pa_response->headers[kUdfRequiredResponseHeader] = "false";
  auction_service::SellerCodeFetchConfig udf_config =
      GetTestSellerUdfConfig(buyer_config);
  MockV8Dispatcher dispatcher;
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  auto executor = std::make_unique<MockExecutor>();
  absl::Notification fetch_done;
  EXPECT_CALL(*curl_http_fetcher, FetchUrlsWithMetadata)
      .WillOnce([&expected_pa_response, &fetch_done](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<HTTPResponse>>)&&>
                        done_callback) {
        std::move(done_callback)({expected_pa_response});
        fetch_done.Notify();
      });

  EXPECT_CALL(dispatcher, LoadSync).Times(0);

  BuyerReportingUdfFetchManager code_fetcher_manager(
      &udf_config, curl_http_fetcher.get(), executor.get(),
      [](const std::string& adtech_code_blob) { return adtech_code_blob; },
      &dispatcher);
  auto status = code_fetcher_manager.Start();
  ASSERT_TRUE(status.ok()) << status;
  fetch_done.WaitForNotification();
  absl::flat_hash_map<std::string, std::string>
      protected_audience_code_blob_per_origin =
          code_fetcher_manager
              .GetProtectedAudienceReportingByOriginForTesting();
  EXPECT_EQ(protected_audience_code_blob_per_origin.size(), 0);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
