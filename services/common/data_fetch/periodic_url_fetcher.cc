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

#include "services/common/data_fetch/periodic_url_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

// Minimal duration to wait before trying to fetch code blobs again.
constexpr absl::Duration kMinUrlFetchDuration = absl::Minutes(1);

PeriodicUrlFetcher::PeriodicUrlFetcher(std::vector<std::string> url_endpoints,
                                       absl::Duration fetch_period_ms,
                                       HttpFetcherAsync* curl_http_fetcher,
                                       server_common::Executor* executor,
                                       absl::Duration time_out_ms)
    : url_endpoints_(std::move(url_endpoints)),
      fetch_period_ms_(fetch_period_ms),
      curl_http_fetcher_(*curl_http_fetcher),
      executor_(*executor),
      time_out_ms_(time_out_ms) {}

absl::Status PeriodicUrlFetcher::Start() {
  CHECK_LT(time_out_ms_, fetch_period_ms_)
      << "Timeout must be less than the fetch period.";
  CHECK_GT(fetch_period_ms_, kMinUrlFetchDuration)
      << "Too small fetch period is prohibited";
  PeriodicUrlFetchSync();
  absl::MutexLock lock(&some_load_success_mu_);
  if (some_load_success_) {
    return absl::OkStatus();
  } else {
    return absl::InternalError("No blob loaded successfully.");
  }
}

void PeriodicUrlFetcher::End() {
  if (task_id_) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

void PeriodicUrlFetcher::PeriodicUrlFetchSync() {
  absl::Notification notification;
  auto done_callback =
      [&notification,
       this](const std::vector<absl::StatusOr<std::string>>& results) mutable {
        bool all_status_ok = true;
        std::vector<std::string> results_value;

        for (const auto& result : results) {
          if (!result.ok()) {
            PS_LOG(ERROR, SystemLogContext())
                << "MultiCurlHttpFetcher Failure Response: " << result.status();
            all_status_ok = false;
            break;
          } else {
            PS_VLOG(kSuccess)
                << "MultiCurlHttpFetcher Success Response: " << result.status();
            results_value.push_back(*result);
          }
        }

        if (all_status_ok) {
          // Vector comparison to only load a new code blob into Roma
          if (cb_results_value_ != results_value) {
            cb_results_value_ = std::move(results_value);
            absl::MutexLock lock(&some_load_success_mu_);
            some_load_success_ = OnFetch(cb_results_value_);
          }
        }
        notification.Notify();
      };

  // Create a HTTPRequest object from the url_endpoint_
  std::vector<HTTPRequest> requests;
  for (const std::string& endpoint : url_endpoints_) {
    PS_VLOG(5) << "Requesting UDF from: " << endpoint;
    requests.push_back(
        {.url = endpoint, .headers = {"Cache-Control: no-cache"}});
  }

  curl_http_fetcher_.FetchUrls(requests, time_out_ms_,
                               std::move(done_callback));
  PS_VLOG(5) << "Waiting for fetch url done notification.";
  notification.WaitForNotification();
  PS_VLOG(5) << "Fetch url wait finished.";
  // Schedules the next code blob fetch and saves that task into task_id_.
  task_id_ = executor_.RunAfter(fetch_period_ms_,
                                [this]() { PeriodicUrlFetchSync(); });
}

}  // namespace privacy_sandbox::bidding_auction_servers
