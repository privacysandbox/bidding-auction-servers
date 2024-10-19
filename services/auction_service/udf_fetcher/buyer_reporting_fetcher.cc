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

#include "services/auction_service/udf_fetcher/buyer_reporting_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/loggers/request_log_context.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::Status BuyerReportingFetcher::Start() {
  if (config_.enable_report_win_url_generation()) {
    PeriodicBuyerReportingFetchSync();
  }
  return absl::OkStatus();
}

void BuyerReportingFetcher::End() {
  if (task_id_) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

absl::flat_hash_map<std::string, std::string>
BuyerReportingFetcher::GetProtectedAuctionReportingByOrigin()
    ABSL_LOCKS_EXCLUDED(code_blob_per_origin_mu_) {
  absl::MutexLock lock(&code_blob_per_origin_mu_);
  return protected_auction_code_blob_per_origin_;
}

absl::flat_hash_map<std::string, std::string>
BuyerReportingFetcher::GetProtectedAppSignalsReportingByOrigin()
    ABSL_LOCKS_EXCLUDED(code_blob_per_origin_mu_) {
  absl::MutexLock lock(&code_blob_per_origin_mu_);
  return protected_app_signals_code_blob_per_origin_;
}

void BuyerReportingFetcher::PeriodicBuyerReportingFetchSync() {
  std::vector<HTTPRequest> requests;
  requests.reserve(
      config_.buyer_report_win_js_urls().size() +
      config_.protected_app_signals_buyer_report_win_js_urls().size());
  std::vector<std::string> buyer_origins;
  for (const auto& [buyer_origin, report_win_endpoint] :
       config_.buyer_report_win_js_urls()) {
    requests.push_back({.url = report_win_endpoint});
    buyer_origins.push_back(buyer_origin);
  }

  for (const auto& [buyer_origin, report_win_endpoint] :
       config_.protected_app_signals_buyer_report_win_js_urls()) {
    requests.push_back({.url = report_win_endpoint});
    buyer_origins.push_back(buyer_origin);
  }

  if (requests.empty()) {
    PS_LOG(ERROR, SystemLogContext())
        << "No buyer reporting UDFs to fetch in config.";
    return;
  }

  absl::Notification fetched;
  auto done_callback =
      [&fetched, &requests, &buyer_origins,
       this](std::vector<absl::StatusOr<std::string>> results) mutable {
        absl::MutexLock lock(&code_blob_per_origin_mu_);
        for (int i = 0; i < results.size(); i++) {
          auto& result = results[i];
          if (!result.ok()) {
            PS_LOG(ERROR, SystemLogContext())
                << "Failed origin " << buyer_origins[i] << " fetch at "
                << requests[i].url << " with status: " << result.status();
          } else {
            if (i < config_.buyer_report_win_js_urls().size()) {
              protected_auction_code_blob_per_origin_[buyer_origins[i]] =
                  *std::move(result);
            } else {
              protected_app_signals_code_blob_per_origin_[buyer_origins[i]] =
                  *std::move(result);
            }
          }
        }
        fetched.Notify();
      };

  http_fetcher_.FetchUrls(requests,
                          absl::Milliseconds(config_.url_fetch_timeout_ms()),
                          std::move(done_callback));
  PS_VLOG(5) << "Waiting for reporting url fetch done notification.";
  fetched.WaitForNotification();
  PS_VLOG(5) << "Reporting url fetch done.";
  // Schedules the next code blob fetch and saves that task into task_id_.
  task_id_ =
      executor_.RunAfter(absl::Milliseconds(config_.url_fetch_period_ms()),
                         [this]() { PeriodicBuyerReportingFetchSync(); });
}

}  // namespace privacy_sandbox::bidding_auction_servers
