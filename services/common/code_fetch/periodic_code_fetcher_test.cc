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

#include "services/common/code_fetch/periodic_code_fetcher.h"

#include <utility>

#include "absl/synchronization/blocking_counter.h"
#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "src/cpp/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(PeriodicCodeFetcherTest, LoadsHttpFetcherResultIntoV8Dispatcher) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockV8Dispatcher dispatcher;
  absl::string_view url = "test.com";
  absl::string_view url_response = "function test(){}";
  absl::Duration fetch_period = absl::Milliseconds(3000);
  auto executor = std::make_unique<MockExecutor>();
  absl::Duration time_out = absl::Milliseconds(1000);
  auto WrapCode = [](const std::string& code_blob) { return "test"; };
  constexpr char kSampleWrappedCode[] = "test";

  absl::BlockingCounter done(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrl)
      .WillOnce([&url, &url_response](
                    HTTPRequest request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        EXPECT_EQ(url, request.url);
        std::move(done_callback)(std::string(url_response));
      });

  EXPECT_CALL(*executor, Run)
      .Times(1)
      .WillOnce([&](absl::AnyInvocable<void()> closure) { closure(); });

  EXPECT_CALL(dispatcher, LoadSync)
      .WillOnce(
          [&done, &kSampleWrappedCode](int version, absl::string_view js) {
            EXPECT_EQ(js, kSampleWrappedCode);
            done.DecrementCount();
            return absl::OkStatus();
          });

  PeriodicCodeFetcher code_fetcher("test.com", fetch_period,
                                   std::move(curl_http_fetcher), dispatcher,
                                   executor.get(), time_out, WrapCode);
  code_fetcher.Start();
  done.Wait();
  code_fetcher.End();
}

TEST(PeriodicCodeFetcherTest, PeriodicallyFetchesCode) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockV8Dispatcher dispatcher;
  absl::string_view url_response = "function test(){}";
  absl::Duration fetch_period = absl::Milliseconds(3000);
  auto executor = std::make_unique<MockExecutor>();
  absl::Duration time_out = absl::Milliseconds(1000);
  auto WrapCode = [](const std::string& code_blob) { return "test"; };

  absl::BlockingCounter done_fetch_url(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrl)
      .Times(1)
      .WillOnce([&url_response, &done_fetch_url](
                    HTTPRequest request, int timeout_ms,
                    absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                        done_callback) {
        done_fetch_url.DecrementCount();
        std::move(done_callback)(std::string(url_response));
      });

  EXPECT_CALL(*executor, Run)
      .Times(1)
      .WillOnce([&](absl::AnyInvocable<void()> closure) { closure(); });

  EXPECT_CALL(*executor, RunAfter)
      .WillOnce([&fetch_period](absl::Duration duration,
                                absl::AnyInvocable<void()> closure) {
        EXPECT_EQ(fetch_period, duration);
        server_common::TaskId id;
        return id;
      });

  PeriodicCodeFetcher code_fetcher("test.com", fetch_period,
                                   std::move(curl_http_fetcher), dispatcher,
                                   executor.get(), time_out, WrapCode);
  code_fetcher.Start();
  done_fetch_url.Wait();
  code_fetcher.End();
}

TEST(PeriodicCodeFetcherTest, LoadsOnlyDifferentHttpFetcherResult) {
  auto curl_http_fetcher = std::make_unique<MockHttpFetcherAsync>();
  MockV8Dispatcher dispatcher;
  absl::string_view url_response = "function test(){}";
  absl::Duration fetch_period = absl::Milliseconds(3000);
  auto executor = std::make_unique<MockExecutor>();
  absl::Duration time_out = absl::Milliseconds(1000);
  auto WrapCode = [](const std::string& code_blob) { return "test"; };
  constexpr char kSampleWrappedCode[] = "test";

  absl::BlockingCounter done_fetch_url(2);
  absl::BlockingCounter done_load_sync(1);
  EXPECT_CALL(*curl_http_fetcher, FetchUrl)
      .Times(2)
      .WillRepeatedly(
          [&url_response, &done_fetch_url](
              HTTPRequest request, int timeout_ms,
              absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                  done_callback) {
            done_fetch_url.DecrementCount();
            std::move(done_callback)(std::string(url_response));
          });

  EXPECT_CALL(*executor, Run)
      .Times(1)
      .WillOnce([&](absl::AnyInvocable<void()> closure) { closure(); });

  EXPECT_CALL(*executor, RunAfter)
      .Times(2)
      .WillOnce([&fetch_period](absl::Duration duration,
                                absl::AnyInvocable<void()> closure) {
        closure();
        server_common::TaskId id;
        return id;
      });

  EXPECT_CALL(dispatcher, LoadSync)
      .Times(1)
      .WillOnce([&done_load_sync, &kSampleWrappedCode](int version,
                                                       absl::string_view js) {
        EXPECT_EQ(js, kSampleWrappedCode);
        done_load_sync.DecrementCount();
        return absl::OkStatus();
      });

  PeriodicCodeFetcher code_fetcher("test.com", fetch_period,
                                   std::move(curl_http_fetcher), dispatcher,
                                   executor.get(), time_out, WrapCode);
  code_fetcher.Start();
  done_load_sync.Wait();
  done_fetch_url.Wait();
  code_fetcher.End();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
