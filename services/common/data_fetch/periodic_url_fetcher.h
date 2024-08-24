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

#ifndef SERVICES_COMMON_DATA_FETCH_PERIODIC_FETCHER_H_
#define SERVICES_COMMON_DATA_FETCH_PERIODIC_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Perioidically fetches the provided URLs.
class PeriodicUrlFetcher : public FetcherInterface {
 public:
  // url_endpoint: a vector of arbitrary endpoints to fetch data from.
  // fetch_period_ms: a time period in between each fetch.
  // curl_http_fetcher: a pointer to a libcurl wrapper client that performs
  // fetching with FetchUrl(). Does not take ownership; pointer must outlive
  // PeriodicUrlFetcher.
  // executor: a raw pointer that takes in a reference of the
  // executor owned by the servers where the instance of this class is also
  // declared. The same executor should be used to construct a HttpFetcherAsync
  // object.
  // time_out_ms: a time out limit for HttpsFetcherAsync client to stop
  // executing FetchUrl.
  explicit PeriodicUrlFetcher(std::vector<std::string> url_endpoints,
                              absl::Duration fetch_period_ms,
                              HttpFetcherAsync* curl_http_fetcher,
                              server_common::Executor* executor,
                              absl::Duration time_out_ms);

  virtual ~PeriodicUrlFetcher() { End(); }

  // Not copyable or movable.
  PeriodicUrlFetcher(const PeriodicUrlFetcher&) = delete;
  PeriodicUrlFetcher& operator=(const PeriodicUrlFetcher&) = delete;

  // Starts a periodic URL fetching process by fetching url with
  // MultiCurlHttpFetcherAsync. Upon every fetch, `OnFetch` is invoked with the
  // fetched results.
  //
  // This may only be called once. Upon invocation it will fetch the URLs once
  // (the fetch may or may not succeed) and will setup a periodic fetch to
  // happen in the background after the method returns.
  absl::Status Start() override;

  // Ends the periodic fetching process by canceling the last scheduled task
  // using task_id_.
  // This may only be called once.
  void End() override;

 protected:
  virtual bool OnFetch(const std::vector<std::string>& fetched_data) = 0;

 private:
  // A private function that fetches the URL(s) synchronously.
  void PeriodicUrlFetchSync();

  std::vector<std::string> url_endpoints_;
  absl::Duration fetch_period_ms_;
  HttpFetcherAsync& curl_http_fetcher_;
  server_common::Executor& executor_;
  absl::Duration time_out_ms_;

  // Keeps track of the next task to be performed on the executor.
  absl::optional<server_common::TaskId> task_id_;
  // Keeps track of the last result.value() returned by FetchUrl and callback
  // function.
  std::vector<std::string> cb_results_value_;

  // Represents a lock on some_load_success_.
  absl::Mutex some_load_success_mu_;
  // Notified when 1 blob is successfully loaded.
  bool some_load_success_ ABSL_GUARDED_BY(some_load_success_mu_) = false;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_DATA_FETCH_PERIODIC_FETCHER_H_
