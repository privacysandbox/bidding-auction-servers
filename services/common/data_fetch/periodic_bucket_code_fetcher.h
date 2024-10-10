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

#ifndef SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_CODE_FETCHER_H_
#define SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_CODE_FETCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/common/clients/code_dispatcher/udf_code_loader_interface.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/data_fetch/periodic_bucket_fetcher.h"
#include "src/concurrent/executor.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class PeriodicBucketCodeFetcher : public PeriodicBucketFetcher {
 public:
  // Constructs a new PeriodicBucketFether.
  explicit PeriodicBucketCodeFetcher(
      absl::string_view bucket_name, absl::Duration fetch_period_ms,
      UdfCodeLoaderInterface* loader, server_common::Executor* executor,
      WrapCodeForDispatch wrap_code,
      google::scp::cpio::BlobStorageClientInterface* blob_storage_client);

  ~PeriodicBucketCodeFetcher() { End(); }

  // Not copyable or movable.
  PeriodicBucketCodeFetcher(const PeriodicBucketCodeFetcher&) = delete;
  PeriodicBucketCodeFetcher& operator=(const PeriodicBucketCodeFetcher&) =
      delete;

 protected:
  // Handles the fetch context (containing the version as well as the data
  // fetched) by loading it into Roma.
  bool OnFetch(const google::scp::core::AsyncContext<
               google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
               google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
                   context) override;

 private:
  // List the blobs in bucket_name_.
  absl::StatusOr<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>
  ListBlobsSync();

  // Callback that handles a GetBlob fetch result.
  void HandleBlobFetchResult(
      const google::scp::core::AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
          google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
          context);

  WrapCodeForDispatch wrap_code_;
  UdfCodeLoaderInterface& loader_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_CODE_FETCHER_H_
