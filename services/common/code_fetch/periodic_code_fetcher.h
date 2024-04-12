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

#ifndef SERVICES_COMMON_CODE_FETCH_PERIODIC_CODE_FETCHER_H_
#define SERVICES_COMMON_CODE_FETCH_PERIODIC_CODE_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/code_fetch/code_fetcher_interface.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// AdTech Code Blob fetching system to update Adtech's GenerateBid(), ScoreAd(),
// ReportWin() and ReportResult() code through a periodic pull mechanism with an
// arbitrary endpoint.
class PeriodicCodeFetcher : public CodeFetcherInterface {
 public:
  // url_endpoint: a vector of arbitrary endpoints to fetch code blobs from.
  // fetch_period_ms: a time period in between each code fetching.
  // curl_http_fetcher: a pointer to a libcurl wrapper client that performs code
  // fetching with FetchUrl(). Does not take ownership; pointer must outlive
  // PeriodicCodeFetcher.
  // dispatcher: a Roma wrapper client that code blob is passed to.
  // executor: a raw pointer that takes in a reference of the
  // executor owned by the servers where the instance of this class is also
  // declared. The same executor should be used to construct a HttpFetcherAsync
  // object.
  // time_out_ms: a time out limit for HttpsFetcherAsync client to stop
  // executing FetchUrl.
  // wrap_code: a lambda function that wraps the code blob for
  // scoring / bidding.
  // version: the version string representing the fetched code; loaded into
  // Roma.
  explicit PeriodicCodeFetcher(
      std::vector<std::string> url_endpoints, absl::Duration fetch_period_ms,
      HttpFetcherAsync* curl_http_fetcher, V8Dispatcher* dispatcher,
      server_common::Executor* executor, absl::Duration time_out_ms,
      WrapCodeForDispatch wrap_code, std::string version);

  ~PeriodicCodeFetcher() { End(); }

  // Not copyable or movable.
  PeriodicCodeFetcher(const PeriodicCodeFetcher&) = delete;
  PeriodicCodeFetcher& operator=(const PeriodicCodeFetcher&) = delete;

  // Starts a periodic code blob fetching process by fetching url with
  // MultiCurlHttpFetcherAsync and loading the updated content into Roma client
  // with V8Dispatcher.
  // This may only be called once.
  absl::Status Start() override;

  // Ends the periodic fetching process by canceling the last scheduled task
  // using task_id_.
  // This may only be called once.
  void End() override;

 private:
  // A private function that performs the actual code fetching process.
  // Synchronous; only returns after attempting to load all fetched blobs.
  void PeriodicCodeFetchSync();

  std::vector<std::string> url_endpoints_;
  absl::Duration fetch_period_ms_;
  HttpFetcherAsync& curl_http_fetcher_;
  V8Dispatcher& dispatcher_;
  server_common::Executor& executor_;
  absl::Duration time_out_ms_;
  WrapCodeForDispatch wrap_code_;
  // Callers should ensure that this version matches with the version provided
  // during the dispatch call. Different versions can help run different code
  // blobs inside Roma even if they have the same entry point names.
  const std::string version_string_;

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

#endif  // SERVICES_COMMON_CODE_FETCH_PERIODIC_CODE_FETCHER_H_
