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

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {

PeriodicCodeFetcher::PeriodicCodeFetcher(
    absl::string_view url_endpoint, absl::Duration fetch_period_ms,
    std::unique_ptr<HttpFetcherAsync> curl_http_fetcher,
    const V8Dispatcher& dispatcher, server_common::Executor* executor,
    absl::Duration time_out_ms,
    absl::AnyInvocable<absl::string_view(absl::string_view)> wrap_code)
    : url_endpoint_(url_endpoint),
      fetch_period_ms_(fetch_period_ms),
      curl_http_fetcher_(std::move(curl_http_fetcher)),
      dispatcher_(dispatcher),
      executor_(std::move(executor)),
      time_out_ms_(time_out_ms),
      wrap_code_(std::move(wrap_code)) {}

void PeriodicCodeFetcher::Start() {
  CHECK_LT(time_out_ms_, fetch_period_ms_)
      << "Timeout must be less than the fetch period.";
  executor_->Run([this]() { PeriodicCodeFetch(); });
}

void PeriodicCodeFetcher::End() {
  if (task_id_.keys != nullptr) {
    executor_->Cancel(std::move(task_id_));
  } else {
    return;
  }
}

void PeriodicCodeFetcher::PeriodicCodeFetch() {
  auto done_callback = [this](absl::StatusOr<std::string> result) mutable {
    if (result.ok()) {
      VLOG(1) << "MultiCurlHttpFetcher Success Response: " << result.status();

      // String comparison to only load a new code blob into Roma
      if (cb_result_value_ != *result) {
        cb_result_value_ = *result;

        absl::string_view wrapped_code = wrap_code_(cb_result_value_);
        absl::Status syncResult = dispatcher_.LoadSync(1, wrapped_code);
        VLOG(1) << "Roma Client Response: " << syncResult;
        if (syncResult.ok()) {
          VLOG(2) << "Current code loaded into Roma:\n" << wrapped_code;
        }
      }
    } else {
      VLOG(1) << "MultiCurlHttpFetcher Failure Response: " << result.status();
    }

    // Schedules the next code blob fetch and saves that task into task_id_.
    task_id_ = executor_->RunAfter(fetch_period_ms_,
                                   [this]() { PeriodicCodeFetch(); });
  };

  // Create a HTTPRequest object from the url_endpoint_
  HTTPRequest request;
  request.url = std::string(url_endpoint_);

  // Convert time_out_ms_ from absl::Duration to int
  int request_time_out = absl::ToInt64Milliseconds(time_out_ms_);

  curl_http_fetcher_->FetchUrl(request, request_time_out,
                               std::move(done_callback));
}

}  // namespace privacy_sandbox::bidding_auction_servers
