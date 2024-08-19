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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_FETCHER_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/periodic_url_fetcher.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class periodically fetches the adtech schema from the configured URL
// and updates the adtech egress schema cache with the fetched schema.
class AdtechSchemaFetcher : public PeriodicUrlFetcher {
 public:
  // url_endpoint: a vector of arbitrary endpoints to fetch code blobs from.
  // fetch_period_ms: a time period in between each code fetching.
  // curl_http_fetcher: a pointer to a libcurl wrapper client that performs code
  // fetching with FetchUrl(). Does not take ownership; pointer must outlive
  // AdtechSchemaFetcher.
  // dispatcher: a Roma wrapper client that code blob is passed to.
  // executor: a raw pointer that takes in a reference of the
  // executor owned by the servers where the instance of this class is also
  // declared. The same executor should be used to construct a HttpFetcherAsync
  // object.
  // time_out_ms: a time out limit for HttpsFetcherAsync client to stop
  // executing FetchUrl.
  // egress_schema_cache: This cache is updated upon periodic adtech schema
  // fetch.
  explicit AdtechSchemaFetcher(std::vector<std::string> url_endpoints,
                               absl::Duration fetch_period_ms,
                               absl::Duration time_out_ms,
                               HttpFetcherAsync* curl_http_fetcher,
                               server_common::Executor* executor,
                               EgressSchemaCache* egress_schema_cache);

  ~AdtechSchemaFetcher() { End(); }

  // Not copyable or movable.
  AdtechSchemaFetcher(const AdtechSchemaFetcher&) = delete;
  AdtechSchemaFetcher& operator=(const AdtechSchemaFetcher&) = delete;

 protected:
  // Loads the fetched code blobs into Roma.
  bool OnFetch(const std::vector<std::string>& fetched_data) override;

 private:
  EgressSchemaCache& egress_schema_cache_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_FETCHER_H_
