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

#ifndef SERVICES_BIDDING_SERVICE_BYOB_BUYER_CODE_FETCH_MANAGER_BYOB_H_
#define SERVICES_BIDDING_SERVICE_BYOB_BUYER_CODE_FETCH_MANAGER_BYOB_H_

#include <memory>
#include <utility>

#include "services/bidding_service/buyer_code_fetch_manager.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kProtectedAuctionExecutableId[] = "bidding_executable";
constexpr char kProtectedAuctionExecutableUrlId[] = "bidding_executable_url";
constexpr char kProtectedAudienceMustBeEnabled[] =
    "Protected Audience must be enabled if buyer code fetch config specifies "
    "an executable.";
constexpr char kProtectedAppSignalsMustBeDisabled[] =
    "Protected App Signals must be disabled if buyer code fetch config "
    "specifies an executable.";

// BuyerCodeFetchManagerByob acts as a wrapper for all logic related to fetching
// bidding service UDF executables to be loaded into ROMA BYOB. Since BYOB does
// not currently support PAS, this class assumes that Protected Audience is
// enabled and PAS is disabled.
class BuyerCodeFetchManagerByob : public BuyerCodeFetchManager {
 public:
  // All raw pointers indicate that we are borrowing a reference and MUST
  // outlive BuyerCodeFetchManagerByob.
  explicit BuyerCodeFetchManagerByob(
      server_common::Executor* executor, HttpFetcherAsync* http_fetcher,
      UdfCodeLoaderInterface* loader,
      std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
          blob_storage_client,
      const bidding_service::BuyerCodeFetchConfig& udf_config)
      : BuyerCodeFetchManager(executor, http_fetcher, loader,
                              std::move(blob_storage_client), udf_config,
                              /*enable_protected_audience=*/true,
                              /*enable_protected_app_signals=*/false) {}

 private:
  absl::Status InitializeLocalCodeFetch() override;

  absl::Status InitializeBucketCodeFetch() override;
  absl::Status InitializeBucketCodeFetchForPA() override;
  absl::Status InitializeBucketCodeFetchForPAS() override;

  absl::Status InitializeUrlCodeFetch() override;
  absl::Status InitializeUrlCodeFetchForPA() override;
  absl::Status InitializeUrlCodeFetchForPAS() override;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BYOB_BUYER_CODE_FETCH_MANAGER_BYOB_H_
