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

#ifndef SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_FETCHER_H_
#define SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_FETCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "src/concurrent/executor.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class PeriodicBucketFetcher : public FetcherInterface {
 public:
  explicit PeriodicBucketFetcher(
      absl::string_view bucket_name, absl::Duration fetch_period_ms,
      server_common::Executor* executor,
      google::scp::cpio::BlobStorageClientInterface* blob_storage_client);

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
  // Callback that handles a GetBlob fetch result.
  virtual absl::Status OnFetch(
      const google::scp::core::AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
          google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
          context) = 0;

  absl::string_view GetBucketName() { return bucket_name_; }

 private:
  // Performs bucket fetching with BlobStorageClient and loads code blob into
  // Roma. Synchronous; only returns after attempting to load each fetched code
  // blob.
  void PeriodicBucketFetchSync();

  // List the blobs in bucket_name_.
  absl::StatusOr<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>
  ListBlobsSync();

  const std::string bucket_name_;
  absl::Duration fetch_period_ms_;
  server_common::Executor& executor_;
  google::scp::cpio::BlobStorageClientInterface& blob_storage_client_;

  // Keeps track of the next task to be performed on the executor.
  absl::optional<server_common::TaskId> task_id_;

  // Represents a lock on some_load_success_.
  absl::Mutex some_load_success_mu_;
  // Notified when 1 blob is successfully loaded.
  bool some_load_success_ ABSL_GUARDED_BY(some_load_success_mu_) = false;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_FETCHER_H_
