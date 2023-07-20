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
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {

PeriodicCodeFetcher::PeriodicCodeFetcher(
    std::vector<std::string> url_endpoints, absl::Duration fetch_period_ms,
    std::unique_ptr<HttpFetcherAsync> curl_http_fetcher,
    const V8Dispatcher& dispatcher, server_common::Executor* executor,
    absl::Duration time_out_ms, WrapCodeForDispatch wrap_code)
    : url_endpoints_(url_endpoints),
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
  auto done_callback =
      [this](std::vector<absl::StatusOr<std::string>> results) mutable {
        bool all_status_ok = true;
        std::vector<std::string> results_value;

        for (const auto& result : results) {
          if (!result.ok()) {
            VLOG(1) << "MultiCurlHttpFetcher Failure Response: "
                    << result.status();
            all_status_ok = false;
            break;
          } else {
            VLOG(1) << "MultiCurlHttpFetcher Success Response: "
                    << result.status();
            results_value.push_back(*result);
          }
        }

        if (all_status_ok) {
          // Vector comparison to only load a new code blob into Roma
          if (cb_results_value_ != results_value) {
            cb_results_value_ = results_value;

            std::string wrapped_code = wrap_code_(cb_results_value_);
            absl::Status syncResult = dispatcher_.LoadSync(1, wrapped_code);
            VLOG(1) << "Roma Client Response: " << syncResult;
            if (syncResult.ok()) {
              VLOG(2) << "Current code loaded into Roma:\n" << wrapped_code;
            }
          }
        }

        // Schedules the next code blob fetch and saves that task into task_id_.
        task_id_ = executor_->RunAfter(fetch_period_ms_,
                                       [this]() { PeriodicCodeFetch(); });
      };

  // Create a HTTPRequest object from the url_endpoint_
  std::vector<HTTPRequest> requests;
  for (std::string endpoint : url_endpoints_) {
    HTTPRequest request;
    request.url = std::string(endpoint);
    requests.push_back(request);
  }

  curl_http_fetcher_->FetchUrls(requests, time_out_ms_,
                                std::move(done_callback));
}

}  // namespace privacy_sandbox::bidding_auction_servers
