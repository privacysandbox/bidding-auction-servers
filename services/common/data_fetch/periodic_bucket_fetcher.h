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

#ifndef SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_FETCHER_H_
#define SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_FETCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/data_fetch/periodic_bucket_fetcher_metrics.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

class PeriodicBucketFetcher : public FetcherInterface {
 public:
  explicit PeriodicBucketFetcher(absl::string_view bucket_name,
                                 absl::Duration fetch_period_ms,
                                 server_common::Executor* executor,
                                 BlobStorageClient* blob_storage_client);

  ~PeriodicBucketFetcher() { End(); }

  // Not copyable or movable.
  PeriodicBucketFetcher(const PeriodicBucketFetcher&) = delete;
  PeriodicBucketFetcher& operator=(const PeriodicBucketFetcher&) = delete;

  // Starts a periodic bucket fetch process with PeriodicBucketFetch().
  // This may only be called once.
  absl::Status Start() override;
  // Ends a periodic bucket fetch process by canceling the last scheduled task.
  // This may only be called once.
  void End() override;

 protected:
  absl::string_view GetBucketName() { return bucket_name_; }

  // Callback that handles a GetBlob fetch result when fetch using PARC.
  virtual absl::Status OnFetch(
      const privacysandbox::apis::parc::v0::GetBlobRequest blob_request,
      const std::string blob_data) = 0;

  // Callback that handles a GetBlob fetch result when fetch using CPIO.
  virtual absl::Status OnFetch(
      const google::scp::core::AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
          google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
          context) = 0;

  // Performs bucket fetching with BlobStorageClient and loads code blob into
  // Roma. Synchronous; only returns after attempting to load each fetched code
  // blob.
  void PeriodicBucketFetchSync();

  const std::string bucket_name_;
  absl::Duration fetch_period_ms_;
  server_common::Executor& executor_;
  BlobStorageClient& blob_storage_client_;

  // Keeps track of the next task to be performed on the executor.
  absl::optional<server_common::TaskId> task_id_;

  // Represents a lock on some_load_success_.
  absl::Mutex some_load_success_mu_;
  // Notified when 1 blob is successfully loaded.
  bool some_load_success_ ABSL_GUARDED_BY(some_load_success_mu_) = false;

 private:
  // List the blobs in bucket_name_ for PARC.
  absl::StatusOr<privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>
  ListBlobsSyncParc();

  // List the blobs in bucket_name_ for CPIO.
  absl::StatusOr<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>
  ListBlobsSyncCpio();
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_DATA_FETCH_PERIODIC_BUCKET_FETCHER_H_
