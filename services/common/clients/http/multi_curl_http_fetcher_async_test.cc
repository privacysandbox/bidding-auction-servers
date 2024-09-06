//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/common/clients/http/multi_curl_http_fetcher_async.h"

#include <string>
#include <utility>

#include <include/gmock/gmock-matchers.h>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "grpc/event_engine/event_engine.h"
#include "grpc/grpc.h"
#include "include/gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "rapidjson/document.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::HasSubstr;

constexpr absl::string_view kUrlA = "https://example.com";
constexpr absl::string_view kUrlB = "https://google.com";
constexpr absl::string_view kUrlC = "https://wikipedia.org";
constexpr absl::string_view kRedirectUrl = "https://wikipedia.com/";
constexpr absl::string_view kFinalUrl = "https://www.wikipedia.org/";
constexpr int kNormalTimeoutMs = 5000;

class MultiCurlHttpFetcherAsyncTest : public ::testing::Test {
 protected:
  MultiCurlHttpFetcherAsyncTest() {
    server_common::log::PS_VLOG_IS_ON(0, 10);
    executor_ = std::make_unique<server_common::EventEngineExecutor>(
        grpc_event_engine::experimental::CreateEventEngine());
    fetcher_ = std::make_unique<MultiCurlHttpFetcherAsync>(executor_.get());
  }

  std::unique_ptr<server_common::EventEngineExecutor> executor_;
  std::unique_ptr<MultiCurlHttpFetcherAsync> fetcher_;
  server_common::GrpcInit gprc_init;
};

TEST_F(MultiCurlHttpFetcherAsyncTest, FetchesUrlSuccessfully) {
  std::string msg;
  absl::BlockingCounter done(1);
  auto done_cb = [&done](absl::StatusOr<std::string> result) {
    done.DecrementCount();
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().length(), 0);
  };
  fetcher_->FetchUrl({kUrlA.begin(), {}}, kNormalTimeoutMs, done_cb);

  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, FetchesUrlWithHeaders) {
  std::string msg;
  absl::BlockingCounter done(1);
  std::vector<std::string> headers = {"X-Random: 2"};
  auto done_cb = [&done](absl::StatusOr<std::string> result) {
    done.DecrementCount();
    ASSERT_TRUE(result.ok());
    // Response has a 'headers' field of the headers sent in the request.
    rapidjson::Document document;
    document.Parse(result.value().c_str());
    rapidjson::Value& headers = document["headers"];
    rapidjson::Value& random_header_val = headers["X-Random"];
    std::string random_header_val_str(random_header_val.GetString());
    EXPECT_EQ(random_header_val_str, "2");
  };
  fetcher_->FetchUrl({"httpbin.org/gzip", headers}, kNormalTimeoutMs, done_cb);

  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest,
       FetchesMultipleUrlsInParallelSuccessfully) {
  int request_count_per_url = 10;
  absl::BlockingCounter done(request_count_per_url * 3);
  auto done_cb = [&done](absl::StatusOr<std::string> result) {
    done.DecrementCount();
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.value().length(), 0);
  };

  while (request_count_per_url) {
    fetcher_->FetchUrl({kUrlA.begin(), {}}, kNormalTimeoutMs, done_cb);
    fetcher_->FetchUrl({kUrlB.begin(), {}}, kNormalTimeoutMs, done_cb);
    fetcher_->FetchUrl({kUrlC.begin(), {}}, kNormalTimeoutMs, done_cb);
    request_count_per_url--;
  }

  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, HandlesTimeoutByReturningError) {
  std::string msg;
  absl::BlockingCounter done(1);
  // NOLINTNEXTLINE
  auto done_cb = [&done](absl::StatusOr<std::string> result) {
    done.DecrementCount();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kDeadlineExceeded);
    EXPECT_THAT(result.status().message(), HasSubstr("Timeout"));
  };
  fetcher_->FetchUrl({kUrlA.begin(), {}}, 1, done_cb);
  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, HandlesMalformattedUrlByReturningError) {
  std::string msg;
  absl::BlockingCounter done(1);
  // NOLINTNEXTLINE
  auto done_cb = [&done](absl::StatusOr<std::string> result) {
    done.DecrementCount();
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_THAT(result.status().message(), HasSubstr("missing URL"));
  };
  fetcher_->FetchUrl({"", {}}, kNormalTimeoutMs, done_cb);
  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, SetsAcceptEncodingHeaderOnRequest) {
  absl::BlockingCounter done(1);
  auto done_cb = [&done](absl::StatusOr<std::string> result) {
    done.DecrementCount();
    ASSERT_TRUE(result.ok());

    // Response has a 'headers' field of the headers sent in the request.
    rapidjson::Document document;
    document.Parse(result.value().c_str());
    rapidjson::Value& headers = document["headers"];
    rapidjson::Value& accept_encoding = headers["Accept-Encoding"];
    std::string accept_encoding_str(accept_encoding.GetString());
    // Verify the Accept-Encoding header sent in the request includes gzip, br,
    // and deflate, the supported decompression algorithms by our cURL library.
    ASSERT_NE(accept_encoding_str.find("gzip"), std::string::npos);
    ASSERT_NE(accept_encoding_str.find("deflate"), std::string::npos);
    ASSERT_NE(accept_encoding_str.find("br"), std::string::npos);
  };

  fetcher_->FetchUrl({"httpbin.org/gzip", {}}, kNormalTimeoutMs, done_cb);
  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, CanFetchMultipleUrlsInParallel) {
  absl::BlockingCounter done(1);
  std::vector<HTTPRequest> test_requests = {
      {kUrlA.begin(), {}}, {kUrlB.begin(), {}}, {kUrlC.begin(), {}}};
  auto done_cb = [&done, &test_requests](
                     const std::vector<absl::StatusOr<std::string>>& results) {
    EXPECT_EQ(results.size(), test_requests.size());
    for (const auto& result : results) {
      ASSERT_TRUE(result.ok()) << result.status();
    }
    done.DecrementCount();
  };

  fetcher_->FetchUrls(test_requests, absl::Milliseconds(kNormalTimeoutMs),
                      std::move(done_cb));
  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, InvokesCallbackForEmptyRequestVector) {
  absl::BlockingCounter done(1);
  std::vector<HTTPRequest> test_requests = {};
  auto done_cb = [&done, &test_requests](
                     const std::vector<absl::StatusOr<std::string>>& results) {
    EXPECT_EQ(results.size(), test_requests.size());
    done.DecrementCount();
  };

  fetcher_->FetchUrls(test_requests, absl::Milliseconds(kNormalTimeoutMs),
                      std::move(done_cb));
  done.Wait();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, PutsUrlSuccessfully) {
  std::string msg;
  absl::Notification notification;
  auto done_cb = [&notification](absl::StatusOr<std::string> result) {
    EXPECT_TRUE(result.ok()) << result.status();
    EXPECT_GT(result.value().length(), 0);
    notification.Notify();
  };
  fetcher_->PutUrl({"http://httpbin.org/put", {}, "{}"}, kNormalTimeoutMs,
                   done_cb);

  notification.WaitForNotification();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, PutsUrlFails) {
  std::string msg;
  absl::Notification notification;
  // NOLINTNEXTLINE
  auto done_cb = [&notification](absl::StatusOr<std::string> result) {
    EXPECT_FALSE(result.ok()) << result.status();
    EXPECT_THAT(result.status().message(),
                HasSubstr("The method is not allowed"));
    notification.Notify();
  };
  fetcher_->PutUrl({"http://httpbin.org", {}, "{}"}, kNormalTimeoutMs, done_cb);

  notification.WaitForNotification();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, FetchUrlsWithMetadataWorks) {
  absl::Notification done;
  std::vector<HTTPRequest> test_requests = {
      {kUrlA.begin(), {}}, {kUrlB.begin(), {}}, {kUrlC.begin(), {}}};
  auto done_cb = [&done, &test_requests](
                     const std::vector<absl::StatusOr<HTTPResponse>>& results) {
    EXPECT_EQ(results.size(), test_requests.size());
    for (const auto& result : results) {
      EXPECT_TRUE(result.ok()) << result.status();
    }
    done.Notify();
  };

  fetcher_->FetchUrlsWithMetadata(
      test_requests, absl::Milliseconds(kNormalTimeoutMs), std::move(done_cb));
  done.WaitForNotification();
}

TEST_F(MultiCurlHttpFetcherAsyncTest,
       FetchUrlsWithMetadataReturnsResponseHeaders) {
  absl::Notification done;
  HTTPRequest request;
  request.url = "httpbin.org/gzip";
  // request.headers = {{"Accept-Encoding", "gzip"}, {"abc", "def"}};
  request.include_headers = {"Content-Encoding"};
  auto done_cb =
      [&done](const std::vector<absl::StatusOr<HTTPResponse>>& results) {
        EXPECT_EQ(results.size(), 1);
        for (const auto& result : results) {
          EXPECT_TRUE(result.ok());
          // Response has a 'headers' field of the headers sent in the request.
          EXPECT_EQ(result.value().headers.size(), 1);
          const auto& it = result.value().headers.find("Content-Encoding");
          EXPECT_NE(it, result.value().headers.end());
          EXPECT_TRUE(it->second.ok()) << it->second.status();
          EXPECT_EQ(it->second.value(), "gzip");
        }
        done.Notify();
      };

  fetcher_->FetchUrlsWithMetadata(
      {request}, absl::Milliseconds(kNormalTimeoutMs), std::move(done_cb));
  done.WaitForNotification();
}

TEST_F(MultiCurlHttpFetcherAsyncTest,
       FetchUrlsWithMetadataReturnsFinalUrlForRedirectEnabled) {
  absl::Notification done;
  HTTPRequest request;
  request.url = kUrlA;
  request.redirect_config.get_redirect_url = true;
  auto done_cb =
      [&done](const std::vector<absl::StatusOr<HTTPResponse>>& results) {
        EXPECT_EQ(results.size(), 1);
        for (const auto& result : results) {
          EXPECT_TRUE(absl::StartsWith(result->final_url, kUrlA))
              << result->final_url;
        }
        done.Notify();
      };

  fetcher_->FetchUrlsWithMetadata(
      {request}, absl::Milliseconds(kNormalTimeoutMs), std::move(done_cb));
  done.WaitForNotification();
}

TEST_F(MultiCurlHttpFetcherAsyncTest,
       FetchUrlsWithMetadataDoesNotReturnFinalUrlIfDisabled) {
  absl::Notification done;
  HTTPRequest request;
  request.url = kUrlA;
  request.redirect_config.get_redirect_url = false;
  auto done_cb =
      [&done](const std::vector<absl::StatusOr<HTTPResponse>>& results) {
        EXPECT_EQ(results.size(), 1);
        for (const auto& result : results) {
          EXPECT_TRUE(result.ok()) << result.status();
          EXPECT_TRUE(result->final_url.empty()) << result->final_url;
        }
        done.Notify();
      };

  fetcher_->FetchUrlsWithMetadata(
      {request}, absl::Milliseconds(kNormalTimeoutMs), std::move(done_cb));
  done.WaitForNotification();
}

TEST_F(MultiCurlHttpFetcherAsyncTest, FetchesUrlAllowsRedirectToNonHttpLinks) {
  absl::Notification done;
  HTTPRequest request;
  request.url = kRedirectUrl;
  request.redirect_config.get_redirect_url = true;
  request.redirect_config.strict_http = true;
  auto done_cb =
      [&done](const std::vector<absl::StatusOr<HTTPResponse>>& results) {
        EXPECT_EQ(results.size(), 1);
        for (const auto& result : results) {
          EXPECT_TRUE(result.ok()) << result.status();
          EXPECT_TRUE(absl::StartsWith(result->final_url, kFinalUrl))
              << result.value().final_url;
        }
        done.Notify();
      };

  fetcher_->FetchUrlsWithMetadata(
      {request}, absl::Milliseconds(kNormalTimeoutMs), std::move(done_cb));
  done.WaitForNotification();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
