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

#ifndef SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_FETCHER_H_
#define SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_FETCHER_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "scp/cc/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "services/common/code_fetch/code_fetcher_interface.h"
#include "src/cpp/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

/* Code Bucket fetching system that updates AdTech's scripts (ex: GenerateBid(),
 * ScoreAd()) by performing GetBlob() from a BlobStorageClient.
 * Periodic fetching starts when Start() is called, and each fetch is done after
 * a certain period of time (not after the last request is executed). End the
 * process by calling End(). This current design can only fetch 1 blob out of 1
 * specific bucket (no support for wasm, reportWin/reportResult, multi-version,
 * etc) since GetBlob() can only fetch 1 blob per request. To support fetching
 * multiple files, my design recommendation is to pass a map of bucket and blob
 * names as inputs (ex: {"bucket A": "blob A", "bucket B": "blob B"}) and call
 * GetBlob() per pair in a loop. However, if you are trying to fetch a large
 * number of blobs or a whole bucket, my recommendation is to create new
 * functions for BlobStorageClient such as GetBucket() that fetches the whole
 * bucket or GetBlobs() that fetch multiple blobs at once. This is so you are
 * making one async call instead of managing multiple and combining the results
 * in this PeriodicBucketFetcher class.
 */

class PeriodicBucketFetcher : public CodeFetcherInterface {
 public:
  // Constructs a new PeriodicBucketFether.
  explicit PeriodicBucketFetcher(
      absl::string_view bucket_name, absl::string_view blob_name,
      absl::Duration fetch_period_ms, V8Dispatcher& dispatcher,
      server_common::Executor* executor,
      std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
          blob_storage_client);

  // Not copyable or movable.
  PeriodicBucketFetcher(const PeriodicBucketFetcher&) = delete;
  PeriodicBucketFetcher& operator=(const PeriodicBucketFetcher&) = delete;

  // Starts a periodic bucket fetch process with PeriodicBucketFetch().
  void Start() override;
  // Ends a periodic bucket fetch process by canceling the last scheduled task.
  void End() override;

 private:
  // Init and run BlobStorageClient. InitCpio is done in
  // bidding/auction_main.cc.
  void InitAndRunConfigClient();
  // Performs bucket fetching with BlobStorageClient and loads code blob into
  // Roma.
  void PeriodicBucketFetch();

  absl::string_view bucket_name_;
  absl::string_view blob_name_;
  absl::Duration fetch_period_ms_;
  V8Dispatcher& dispatcher_;
  server_common::Executor* executor_;
  std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
      blob_storage_client_;

  // Keeps track of the last fetched value for comparison. Code is only loaded
  // into Roma if the fetched result is different from the previous value.
  std::string cb_result_value_;
  // Keeps track of the next task to be performed on the executor.
  server_common::TaskId task_id_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CODE_FETCH_PERIODIC_BUCKET_FETCHER_H_
