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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/auction_service/udf_fetcher/adtech_code_version_util.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::Status BuyerReportingUdfFetchManager::Start() {
  if (config_.enable_report_win_url_generation()) {
    return PeriodicBuyerReportingFetchAndLoadSync();
  }
  return absl::OkStatus();
}

void BuyerReportingUdfFetchManager::End() {
  if (task_id_.has_value()) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

absl::flat_hash_map<std::string, std::string>
BuyerReportingUdfFetchManager::GetProtectedAudienceReportingByOriginForTesting()
    ABSL_LOCKS_EXCLUDED(code_blob_per_origin_mu_) {
  absl::MutexLock lock(&code_blob_per_origin_mu_);
  return protected_auction_code_blob_per_origin_;
}

absl::flat_hash_map<std::string, std::string> BuyerReportingUdfFetchManager::
    GetProtectedAppSignalsReportingByOriginForTesting()
        ABSL_LOCKS_EXCLUDED(code_blob_per_origin_mu_) {
  absl::MutexLock lock(&code_blob_per_origin_mu_);
  return protected_app_signals_code_blob_per_origin_;
}

void BuyerReportingUdfFetchManager::LoadBuyerCode(
    const std::string& version, const std::string& fetched_blob) {
  std::string wrapped_code = buyer_code_wrapper_(fetched_blob);
  absl::Status syncResult = dispatcher_.LoadSync(version, wrapped_code);
  if (syncResult.ok()) {
    PS_VLOG(kSuccess) << "Roma loaded buyer reporting udf version: " << version
                      << " with contents:\n"
                      << wrapped_code;
  } else {
    PS_LOG(ERROR) << "Roma  LoadSync fail for buyer: " << syncResult;
  }
}

void BuyerReportingUdfFetchManager::OnProtectedAudienceUdfFetch(
    const std::string& buyer_origin, std::string& udf) {
  absl::MutexLock lock(&code_blob_per_origin_mu_);
  if (protected_auction_code_blob_per_origin_[buyer_origin] != udf) {
    absl::StatusOr<std::string> code_version =
        GetBuyerReportWinVersion(buyer_origin, AuctionType::kProtectedAudience);
    if (!code_version.ok()) {
      PS_LOG(ERROR) << "Error getting code version for buyer:" << buyer_origin
                    << ";Error:" << code_version.status().message();
      return;
    }
    LoadBuyerCode(*code_version, udf);
    protected_auction_code_blob_per_origin_[buyer_origin] = std::move(udf);
  }
}

void BuyerReportingUdfFetchManager::OnProtectedAppSignalUdfFetch(
    const std::string& buyer_origin, std::string& udf) {
  absl::MutexLock lock(&code_blob_per_origin_mu_);
  if (protected_app_signals_code_blob_per_origin_[buyer_origin] != udf) {
    absl::StatusOr<std::string> code_version = GetBuyerReportWinVersion(
        buyer_origin, AuctionType::kProtectedAppSignals);
    if (!code_version.ok()) {
      PS_LOG(ERROR) << "Error getting code version for buyer:" << buyer_origin
                    << ";Error:" << code_version.status().message();
    }
    LoadBuyerCode(code_version.value(), udf);
    protected_app_signals_code_blob_per_origin_[buyer_origin] = std::move(udf);
  }
}

absl::Status
BuyerReportingUdfFetchManager::PeriodicBuyerReportingFetchAndLoadSync() {
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
    PS_LOG(WARNING) << "No buyer reporting UDFs to fetch in config.";
    return absl::OkStatus();
  }

  absl::Notification fetched_and_loaded;
  auto done_callback =
      [&fetched_and_loaded, &requests, &buyer_origins,
       this](std::vector<absl::StatusOr<std::string>> results) mutable {
        for (int i = 0; i < results.size(); i++) {
          auto& result = results[i];
          if (!result.ok()) {
            PS_LOG(ERROR) << "Failed origin " << buyer_origins[i]
                          << " fetch at " << requests[i].url
                          << " with status: " << result.status();
            continue;
          }
          if (i < config_.buyer_report_win_js_urls().size()) {
            OnProtectedAudienceUdfFetch(buyer_origins[i], *result);
          } else {
            OnProtectedAppSignalUdfFetch(buyer_origins[i], *result);
          }
        }
        fetched_and_loaded.Notify();
      };

  http_fetcher_.FetchUrls(requests,
                          absl::Milliseconds(config_.url_fetch_timeout_ms()),
                          std::move(done_callback));
  PS_VLOG(kPlain)
      << "Waiting for reporting udf fetch and load done notification.";
  fetched_and_loaded.WaitForNotification();
  // Verify if all the udfs were fetched and loaded successfully.
  if (config_.buyer_report_win_js_urls().size() +
          config_.protected_app_signals_buyer_report_win_js_urls().size() !=
      GetProtectedAudienceReportingByOriginForTesting().size() +
          GetProtectedAppSignalsReportingByOriginForTesting().size()) {
    PS_LOG(ERROR)
        << "Error fetching and loading one or more buyer's reportWin() udf.";
  } else {
    PS_VLOG(kPlain) << "Reporting udf fetch and load done.";
  }
  // Schedules the next code blob fetch and saves that task into task_id_.
  task_id_ = executor_.RunAfter(
      absl::Milliseconds(config_.url_fetch_period_ms()), [this]() {
        if (!PeriodicBuyerReportingFetchAndLoadSync().ok()) {
          PS_LOG(ERROR) << "Error fetching and loading reportWin udf for one "
                           "or more buyers";
        }
      });
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers