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

#ifndef SERVICES_BUYER_REPORTING_FETCHER_H_
#define SERVICES_BUYER_REPORTING_FETCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "services/auction_service/udf_fetcher/auction_code_fetch_config.pb.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Periodically fetch all code at buyer reporting urls.
// Provide an API to allow callers to retrieve the content
// of the urls.
class BuyerReportingFetcher : public FetcherInterface {
 public:
  [[deprecated]] explicit BuyerReportingFetcher(
      const auction_service::SellerCodeFetchConfig& config,
      HttpFetcherAsync* http_fetcher, server_common::Executor* executor)
      : config_(config), http_fetcher_(*http_fetcher), executor_(*executor) {}

  ~BuyerReportingFetcher() { End(); }

  // Not copyable or movable.
  [[deprecated]] BuyerReportingFetcher(const BuyerReportingFetcher&) = delete;
  [[deprecated]] BuyerReportingFetcher& operator=(
      const BuyerReportingFetcher&) = delete;

  // Starts a periodic code blob fetching process by fetching url with
  // MultiCurlHttpFetcherAsync and loading the updated content into a publicly
  // accessible map.
  [[deprecated]] absl::Status Start() override;

  // Ends the periodic fetching process by canceling the last scheduled task
  // using task_id_.
  [[deprecated]] void End() override;

  // WARNING: do not use in critical paths, this returns a copy of a map.
  // return a map of buyer origin to reporting blob
  [[deprecated]] absl::flat_hash_map<std::string, std::string>
  GetProtectedAuctionReportingByOrigin()
      ABSL_LOCKS_EXCLUDED(code_blob_per_origin_mu_);

  // WARNING: do not use in critical paths, this returns a copy of a map.
  // return a map of buyer origin to reporting blob
  [[deprecated]] absl::flat_hash_map<std::string, std::string>
  GetProtectedAppSignalsReportingByOrigin()
      ABSL_LOCKS_EXCLUDED(code_blob_per_origin_mu_);

 private:
  void PeriodicBuyerReportingFetchSync();

  // Acts as a lock on the code_blob_per_origin_ maps.
  absl::Mutex code_blob_per_origin_mu_;
  // Keeps track of the next task to be performed on the executor.
  std::optional<server_common::TaskId> task_id_;

  const auction_service::SellerCodeFetchConfig& config_;

  // Configured as a constructor parameter to aid testing.
  HttpFetcherAsync& http_fetcher_;
  server_common::Executor& executor_;

  absl::flat_hash_map<std::string, std::string>
      protected_auction_code_blob_per_origin_
          ABSL_GUARDED_BY(code_blob_per_origin_mu_);
  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_code_blob_per_origin_
          ABSL_GUARDED_BY(code_blob_per_origin_mu_);
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_REPORTING_FETCHER_H_
