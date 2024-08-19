//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_BUYER_CODE_FETCH_MANAGER_H_
#define SERVICES_BUYER_CODE_FETCH_MANAGER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "services/bidding_service/bidding_code_fetch_config.pb.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/periodic_bucket_code_fetcher.h"
#include "services/common/data_fetch/periodic_code_fetcher.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kProtectedAuctionJsId[] = "bidding_js";
constexpr char kProtectedAppSignalsJsId[] = "protected_app_signals_bidding_js";
constexpr char kAdsRetrievalJsId[] = "prepare_data_for_ads_retrieval_js";
constexpr char kFetchModeInvalid[] = "Fetch mode invalid.";
constexpr char kLocalFetchNeedsPath[] =
    "Local fetch mode requires a non-empty path.";
constexpr char kBlobStorageClientInitFailed[] =
    "Failed to init cloud blob storage client.";
constexpr char kEmptyBucketName[] = "Empty bucket name for ";
constexpr char kEmptyBucketDefault[] =
    "Bucket fetch mode requires a non-empty bucket default version object for ";
constexpr char kFailedBucketFetchStartup[] = "Failed bucket fetch startup for ";

// BuyerCodeFetchManager acts as a wrapper for all logic related to fetching
// bidding service UDFs. This class consumes a BuyerCodeFetchConfig and uses it,
// along with various other dependencies, to create and own all instances of
// FetcherInterface in the bidding service.
class BuyerCodeFetchManager {
 public:
  // All raw pointers indicate that we are borrowing a reference and MUST
  // outlive BuyerCodeFetchManager.
  explicit BuyerCodeFetchManager(
      server_common::Executor* executor, HttpFetcherAsync* http_fetcher,
      V8Dispatcher* dispatcher,
      std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
          blob_storage_client,
      const bidding_service::BuyerCodeFetchConfig& udf_config,
      bool enable_protected_audience, bool enable_protected_app_signals)
      : executor_(*executor),
        http_fetcher_(*http_fetcher),
        dispatcher_(*dispatcher),
        blob_storage_client_(std::move(blob_storage_client)),
        udf_config_(udf_config),
        enable_protected_audience_(enable_protected_audience),
        enable_protected_app_signals_(enable_protected_app_signals) {}

  ~BuyerCodeFetchManager();

  // Not copyable or movable.
  BuyerCodeFetchManager(const BuyerCodeFetchManager&) = delete;
  BuyerCodeFetchManager& operator=(const BuyerCodeFetchManager&) = delete;

  // Must be called exactly once. Failure to Init means that the bidding service
  // has not successfully loaded any UDFs and is unable to serve any requests.
  // A successful Init means that Roma has succeeded in loading a UDF.
  absl::Status Init();

 private:
  // Must be called exactly once. This should only be called on server shutdown,
  // and only after Init has returned (either a success or error is fine).
  // Failure to End means there was an issue releasing resources and should
  // be investigated to ensure that requests are being terminated gracefully.
  absl::Status End();

  absl::Status InitializeLocalCodeFetch();

  absl::Status InitBucketClient();
  absl::Status InitializeBucketCodeFetch();
  absl::Status InitializeBucketCodeFetchForPA();
  absl::Status InitializeBucketCodeFetchForPAS();

  absl::StatusOr<std::unique_ptr<FetcherInterface>> StartBucketFetch(
      const std::string& bucket_name, const std::string& default_version,
      absl::string_view script_logging_name, absl::Duration url_fetch_period_ms,
      absl::AnyInvocable<std::string(const std::vector<std::string>&)>
          wrap_code);

  absl::Status InitializeUrlCodeFetch();
  absl::Status InitializeUrlCodeFetchForPA();
  absl::Status InitializeUrlCodeFetchForPAS();

  absl::StatusOr<std::unique_ptr<FetcherInterface>> StartUrlFetch(
      const std::string& js_url, const std::string& wasm_helper_url,
      const std::string& roma_version, absl::string_view script_logging_name,
      absl::Duration url_fetch_period_ms, absl::Duration url_fetch_timeout_ms,
      absl::AnyInvocable<std::string(const std::vector<std::string>&)>
          wrap_code);

  server_common::Executor& executor_;
  HttpFetcherAsync& http_fetcher_;
  V8Dispatcher& dispatcher_;
  std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
      blob_storage_client_;
  const bidding_service::BuyerCodeFetchConfig udf_config_;
  const bool enable_protected_audience_;
  const bool enable_protected_app_signals_;

  std::unique_ptr<FetcherInterface> pa_udf_fetcher_;
  std::unique_ptr<FetcherInterface> pas_bidding_udf_fetcher_;
  std::unique_ptr<FetcherInterface> pas_ads_retrieval_udf_fetcher_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_CODE_FETCH_MANAGER_H_
