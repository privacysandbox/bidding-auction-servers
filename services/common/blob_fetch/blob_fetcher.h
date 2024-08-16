/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_H_
#define SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/concurrent/executor.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// Blob fetching system to read AdTech's files from the cloud storage buckets.
// TODO(b/316960066): Support periodic fetching.
// TODO(b/316960066): Write the common lib with PeriodicBucketFetcher
class BlobFetcher {
 public:
  // A pair of file path and byte string.
  struct Blob {
    std::string path;
    std::string bytes;

    Blob(const std::string& path, const std::string& bytes)
        : path(path), bytes(bytes) {}
  };

  // Constructs a new BlobFetcher.
  // `bucket_name`: The cloud storage bucket name to read from.
  BlobFetcher(absl::string_view bucket_name, server_common::Executor* executor,
              std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
                  blob_storage_client);

  // Not copyable or movable.
  BlobFetcher(const BlobFetcher&) = delete;
  BlobFetcher& operator=(const BlobFetcher&) = delete;

  const std::vector<Blob>& snapshot() const { return snapshot_; }

  // Fetches the bucket synchronously.
  absl::Status FetchSync();

 private:
  // Performs bucket fetching with BlobStorageClient.
  // It's not thread-safe.
  absl::Status InternalFetch();

  const std::string bucket_name_;
  server_common::Executor* executor_;  // not owned
  std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
      blob_storage_client_;
  // Keeps the latest snapshot of the storage bucket.
  std::vector<Blob> snapshot_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_BLOB_FETCH_BLOB_FETCHER_H_
