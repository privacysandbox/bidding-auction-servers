/*
 * Copyright 2023 Google LLC
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

#include "services/common/data_fetch/periodic_code_fetcher.h"

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr char kDefaultVerison[] = "v1";

class PeriodicCodeFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

TEST_F(PeriodicCodeFetcherTest, LoadsHttpFetcherResultIntoCodeLoader) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockUdfCodeLoaderInterface dispatcher;
  absl::string_view js_url = "js.com";
  absl::string_view wasm_helper_url = "wasm.com";
  std::vector<absl::StatusOr<std::string>> url_response = {"function test(){}"};

  const std::vector<std::string>& endpoints = {"js.com", "wasm.com"};
  absl::Duration fetch_period = absl::Minutes(3);
  auto executor = std::make_unique<MockExecutor>();
  absl::Duration time_out = absl::Milliseconds(1000);
  auto WrapCode = [](const std::vector<std::string>& adtech_code_blobs) {
    return "test";
  };
  constexpr char kSampleWrappedCode[] = "test";

  absl::BlockingCounter done(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrls)
      .WillOnce([&js_url, &wasm_helper_url, &url_response](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<std::string>>)&&>
                        done_callback) {
        EXPECT_EQ(js_url, requests.at(0).url);
        EXPECT_EQ(wasm_helper_url, requests.at(1).url);
        std::move(done_callback)(url_response);
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&done, &kSampleWrappedCode](std::string_view version,
                                             absl::string_view js) {
        EXPECT_EQ(version, kDefaultVerison);
        EXPECT_EQ(js, kSampleWrappedCode);
        done.DecrementCount();
        return absl::OkStatus();
      });

  PeriodicCodeFetcher code_fetcher(
      endpoints, fetch_period, curl_http_fetcher.get(), &dispatcher,
      executor.get(), time_out, WrapCode, kDefaultVerison);
  auto status = code_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  code_fetcher.End();
}

TEST_F(PeriodicCodeFetcherTest, PeriodicallyFetchesCode) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockUdfCodeLoaderInterface dispatcher;
  std::vector<absl::StatusOr<std::string>> url_response = {"function test(){}"};

  const std::vector<std::string>& endpoints = {"test.com"};
  absl::Duration fetch_period = absl::Minutes(3);
  auto executor = std::make_unique<MockExecutor>();
  absl::Duration time_out = absl::Milliseconds(1000);
  auto WrapCode = [](const std::vector<std::string>& adtech_code_blobs) {
    return "test";
  };

  absl::BlockingCounter done_fetch_url(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrls)
      .Times(1)
      .WillOnce([&url_response, &done_fetch_url](
                    const std::vector<HTTPRequest>& requests,
                    absl::Duration timeout,
                    absl::AnyInvocable<void(
                        std::vector<absl::StatusOr<std::string>>)&&>
                        done_callback) {
        done_fetch_url.DecrementCount();
        std::move(done_callback)(url_response);
      });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([&fetch_period](absl::Duration duration,
                                absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(fetch_period, duration);
        server_common::TaskId id;
        return id;
      });

  PeriodicCodeFetcher code_fetcher(
      endpoints, fetch_period, curl_http_fetcher.get(), &dispatcher,
      executor.get(), time_out, WrapCode, kDefaultVerison);
  auto status = code_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done_fetch_url.Wait();
  code_fetcher.End();
}

TEST_F(PeriodicCodeFetcherTest, LoadsOnlyDifferentHttpFetcherResult) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockUdfCodeLoaderInterface dispatcher;
  std::vector<absl::StatusOr<std::string>> url_response = {"function test(){}"};

  const std::vector<std::string>& endpoints = {"test.com"};
  absl::Duration fetch_period = absl::Minutes(3);
  auto executor = std::make_unique<MockExecutor>();
  absl::Duration time_out = absl::Milliseconds(1000);
  auto WrapCode = [](const std::vector<std::string>& adtech_code_blobs) {
    return "test";
  };
  constexpr char kSampleWrappedCode[] = "test";

  absl::BlockingCounter done_fetch_url(2);
  absl::BlockingCounter done_load_sync(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrls)
      .Times(2)
      .WillRepeatedly([&url_response, &done_fetch_url](
                          const std::vector<HTTPRequest>& requests,
                          absl::Duration timeout,
                          absl::AnyInvocable<void(
                              std::vector<absl::StatusOr<std::string>>)&&>
                              done_callback) {
        done_fetch_url.DecrementCount();
        std::move(done_callback)(url_response);
      });

  EXPECT_CALL(*executor, RunAfter)
      .Times(2)
      .WillOnce(
          [](absl::Duration duration, absl::AnyInvocable<void()> closure) {
            closure();
            server_common::TaskId id;
            return id;
          });

  EXPECT_CALL(dispatcher, LoadSync)
      .Times(1)
      .WillOnce([&done_load_sync, &kSampleWrappedCode](std::string_view version,
                                                       absl::string_view js) {
        EXPECT_EQ(js, kSampleWrappedCode);
        done_load_sync.DecrementCount();
        return absl::OkStatus();
      });

  PeriodicCodeFetcher code_fetcher(
      endpoints, fetch_period, curl_http_fetcher.get(), &dispatcher,
      executor.get(), time_out, WrapCode, kDefaultVerison);
  auto status = code_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done_load_sync.Wait();
  done_fetch_url.Wait();
  code_fetcher.End();
}

TEST_F(PeriodicCodeFetcherTest, LoadsCodeWithTheCorrectVersion) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockUdfCodeLoaderInterface dispatcher;
  auto executor = std::make_unique<MockExecutor>();
  auto wrap_code = [](const std::vector<std::string>& adtech_code_blobs) {
    return "test";
  };

  absl::BlockingCounter done(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrls)
      .WillOnce([](const std::vector<HTTPRequest>& requests,
                   absl::Duration timeout,
                   absl::AnyInvocable<void(
                       std::vector<absl::StatusOr<std::string>>)&&>
                       done_callback) { std::move(done_callback)({""}); });

  constexpr char kTestVersion[] = "v10";
  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce([&done, kTestVersion](std::string_view observed_version,
                                      absl::string_view js) {
        EXPECT_EQ(observed_version, kTestVersion);
        done.DecrementCount();
        return absl::OkStatus();
      });

  PeriodicCodeFetcher code_fetcher(
      {"code.com"}, absl::Minutes(2), curl_http_fetcher.get(), &dispatcher,
      executor.get(), absl::Milliseconds(100), wrap_code, kTestVersion);
  auto status = code_fetcher.Start();
  ASSERT_TRUE(status.ok()) << status;
  done.Wait();
  code_fetcher.End();
}

TEST_F(PeriodicCodeFetcherTest, LoadFetchFrequencyMustBeGreaterThan1Min) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  EXPECT_CALL(*curl_http_fetcher, FetchUrls).Times(0);

  auto executor = std::make_unique<MockExecutor>();
  EXPECT_CALL(*executor, Run).Times(0);

  MockUdfCodeLoaderInterface dispatcher;
  EXPECT_CALL(dispatcher, LoadSync).Times(0);

  PeriodicCodeFetcher code_fetcher(
      {"fake-test-code-blob.com"}, absl::Minutes(1), curl_http_fetcher.get(),
      &dispatcher, executor.get(), absl::Milliseconds(100),
      [](const std::vector<std::string>& ad_tech_code_blobs) { return ""; },
      /*version=*/"v25");
  EXPECT_DEATH(auto status = code_fetcher.Start(), "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
