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

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_BUCKET_FETCHER_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_BUCKET_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/common/data_fetch/periodic_bucket_fetcher.h"
#include "src/concurrent/executor.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class EgressSchemaBucketFetcher : public PeriodicBucketFetcher {
 public:
  // bucket_name: the name of the bucket to fetch schemas from.
  // fetch_period_ms: a time period in between each schema fetching.
  // executor: a raw pointer that takes in a reference of the
  // executor owned by the servers where the instance of this class is also
  // declared.
  // blob_storage_client: a pointer to a blob storage client that performs
  // schema fetching with GetBlob(). Does not take ownership; pointer must
  // outlive EgressSchemaBucketFetcher.
  // egress_schema_cache: This cache is updated upon periodic egress schema
  // fetch.
  explicit EgressSchemaBucketFetcher(
      absl::string_view bucket_name, absl::Duration fetch_period_ms,
      server_common::Executor* executor,
      google::scp::cpio::BlobStorageClientInterface* blob_storage_client,
      EgressSchemaCache* egress_schema_cache);

  ~EgressSchemaBucketFetcher() { End(); }

  // Not copyable or movable.
  EgressSchemaBucketFetcher(const EgressSchemaBucketFetcher&) = delete;
  EgressSchemaBucketFetcher& operator=(const EgressSchemaBucketFetcher&) =
      delete;

 protected:
  // Loads the fetched code blobs into EgressSchemaCache.
  absl::Status OnFetch(
      const google::scp::core::AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
          google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
          context) override;

 private:
  EgressSchemaCache& egress_schema_cache_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_BUCKET_FETCHER_H_
