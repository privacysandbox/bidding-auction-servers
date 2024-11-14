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

#include "services/bidding_service/egress_features/egress_schema_bucket_fetcher.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "services/common/data_fetch/version_util.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"
#include "src/logger/request_context_logger.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

EgressSchemaBucketFetcher::EgressSchemaBucketFetcher(
    absl::string_view bucket_name, absl::Duration fetch_period_ms,
    server_common::Executor* executor,
    google::scp::cpio::BlobStorageClientInterface* blob_storage_client,
    EgressSchemaCache* egress_schema_cache)
    : PeriodicBucketFetcher(bucket_name, fetch_period_ms, executor,
                            blob_storage_client),
      egress_schema_cache_(*egress_schema_cache) {}

bool EgressSchemaBucketFetcher::OnFetch(
    const google::scp::core::AsyncContext<
        google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
        google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>&
        context) {
  const absl::StatusOr<std::string> blob_name = GetBucketBlobVersion(
      GetBucketName(), context.request->blob_metadata().blob_name());

  if (!blob_name.ok()) {
    PS_LOG(ERROR) << "Failed to fetch schema bucket blob name "
                  << blob_name.status();
    return false;
  }
  if (!context.result.Successful()) {
    PS_LOG(ERROR) << "Failed to fetch egress schema " << *blob_name;
    return false;
  }

  const std::string& schema = context.response->blob().data();

  if (absl::Status status = egress_schema_cache_.Update(schema, *blob_name);
      !status.ok()) {
    PS_LOG(ERROR) << "Failed to update egress cache with fetched schema:\n"
                  << schema << "\n"
                  << status;
    return false;
  }
  PS_VLOG(kSuccess) << "Loaded egress schema:\n" << schema;
  return true;
}

}  // namespace privacy_sandbox::bidding_auction_servers
