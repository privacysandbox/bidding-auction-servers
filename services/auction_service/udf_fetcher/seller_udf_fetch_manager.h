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

#ifndef SERVICES_SELLER_UDF_FETCH_MANAGER_H_
#define SERVICES_SELLER_UDF_FETCH_MANAGER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/udf_fetcher/auction_code_fetch_config.pb.h"
#include "services/auction_service/udf_fetcher/buyer_reporting_fetcher.h"
#include "services/auction_service/udf_fetcher/buyer_reporting_udf_fetch_manager.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/data_fetch/periodic_bucket_code_fetcher.h"
#include "services/common/data_fetch/periodic_code_fetcher.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kBuyerReportingFailedStartup[] =
    "Buyer reporting code fetcher failed startup.";
constexpr char kSellerUDFLoadFailedStartup[] =
    "Failed loading code blob on startup.";

// SellerUdfFetchManager acts as a wrapper for all logic related to fetching
// auction service UDFs. This class consumes a SellerCodeFetchConfig and uses
// it, along with various other dependencies, to create and own all instances of
// FetcherInterface in the auction service.
class SellerUdfFetchManager {
 public:
  SellerUdfFetchManager(
      std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
          blob_storage_client,
      server_common::Executor* executor, HttpFetcherAsync* seller_http_fetcher,
      HttpFetcherAsync* buyer_reporting_http_fetcher, V8Dispatcher* dispatcher,
      const auction_service::SellerCodeFetchConfig& udf_config,
      bool enable_protected_app_signals)
      : executor_(*executor),
        seller_http_fetcher_(*seller_http_fetcher),
        buyer_reporting_http_fetcher_(*buyer_reporting_http_fetcher),
        dispatcher_(*dispatcher),
        enable_protected_app_signals_(enable_protected_app_signals),
        udf_config_(udf_config),
        blob_storage_client_(std::move(blob_storage_client)) {}

  // Not copyable or movable.
  SellerUdfFetchManager(const SellerUdfFetchManager&) = delete;
  SellerUdfFetchManager& operator=(const SellerUdfFetchManager&) = delete;

  absl::Status Init();

  absl::Status End();

  // Configures the runtime versioning defaults to be used by reactors.
  absl::Status ConfigureRuntimeDefaults(
      AuctionServiceRuntimeConfig& runtime_config);

 private:
  WrapCodeForDispatch GetUdfWrapper();
  WrapSingleCodeBlobForDispatch GetUdfWrapperForBuyer();

  // This function will replace GetUdfWrapper once seller and buyer code
  // isolation is enforced
  WrapCodeForDispatch GetUdfWrapperForSeller();

  absl::Status InitializeLocalCodeFetch();

  absl::Status InitBucketClient();

  absl::StatusOr<std::unique_ptr<PeriodicBucketCodeFetcher>>
  InitializeBucketCodeFetch();

  absl::StatusOr<std::unique_ptr<PeriodicCodeFetcher>> InitializeUrlCodeFetch();

  server_common::Executor& executor_;
  HttpFetcherAsync& seller_http_fetcher_;
  HttpFetcherAsync& buyer_reporting_http_fetcher_;
  V8Dispatcher& dispatcher_;
  const bool enable_protected_app_signals_;
  const auction_service::SellerCodeFetchConfig& udf_config_;
  std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
      blob_storage_client_;

  std::unique_ptr<BuyerReportingFetcher> buyer_reporting_fetcher_;
  std::unique_ptr<BuyerReportingUdfFetchManager>
      buyer_reporting_udf_fetch_manager_;
  std::unique_ptr<FetcherInterface> seller_code_fetcher_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_UDF_FETCH_MANAGER_H_
