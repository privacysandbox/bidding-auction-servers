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

#include "services/bidding_service/egress_features/adtech_schema_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

AdtechSchemaFetcher::AdtechSchemaFetcher(std::vector<std::string> url_endpoints,
                                         absl::Duration fetch_period_ms,
                                         absl::Duration time_out_ms,
                                         HttpFetcherAsync* curl_http_fetcher,
                                         server_common::Executor* executor,
                                         EgressSchemaCache* egress_schema_cache)
    : PeriodicUrlFetcher(std::move(url_endpoints), fetch_period_ms,
                         curl_http_fetcher, executor, time_out_ms),
      egress_schema_cache_(*egress_schema_cache) {}

bool AdtechSchemaFetcher::OnFetch(
    const std::vector<std::string>& fetched_data) {
  DCHECK_EQ(fetched_data.size(), 1) << "Expect only one schema to be fetched";
  PS_VLOG(5) << "Fetched schemas: " << absl::StrJoin(fetched_data, "\n\n");
  if (absl::Status status = egress_schema_cache_.Update(fetched_data[0]);
      !status.ok()) {
    PS_LOG(ERROR)
        << "Failed to update adtech egress cache with fetched schema:\n"
        << fetched_data[0] << "\n"
        << status;
    return false;
  }
  PS_VLOG(5) << "Loaded adtech egress schema:\n" << fetched_data[0];
  return true;
}

}  // namespace privacy_sandbox::bidding_auction_servers
