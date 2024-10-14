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

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

PeriodicCodeFetcher::PeriodicCodeFetcher(
    std::vector<std::string> url_endpoints, absl::Duration fetch_period_ms,
    HttpFetcherAsync* curl_http_fetcher, UdfCodeLoaderInterface* loader,
    server_common::Executor* executor, absl::Duration time_out_ms,
    WrapCodeForDispatch wrap_code, std::string version_string)
    : PeriodicUrlFetcher(std::move(url_endpoints), fetch_period_ms,
                         curl_http_fetcher, executor, time_out_ms),
      loader_(*loader),
      wrap_code_(std::move(wrap_code)),
      version_string_(std::move(version_string)) {}

bool PeriodicCodeFetcher::OnFetch(
    const std::vector<std::string>& fetched_data) {
  std::string wrapped_code = wrap_code_(fetched_data);
  // Construct the success log message before calling LoadSync so that we can
  // move the code.
  std::string success_log_message =
      absl::StrCat("Current code loaded into Roma for version ",
                   version_string_, ":\n", wrapped_code);
  absl::Status sync_result =
      loader_.LoadSync(version_string_, std::move(wrapped_code));
  if (sync_result.ok()) {
    PS_VLOG(kSuccess) << success_log_message;
    return true;
  }
  PS_LOG(ERROR, SystemLogContext()) << "Roma LoadSync fail: " << sync_result;
  return false;
}

}  // namespace privacy_sandbox::bidding_auction_servers
