/*
 * Copyright 2025 Google LLC
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

#include <future>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "services/common/data_fetch/periodic_bucket_fetcher.h"
#include "services/common/data_fetch/periodic_bucket_fetcher_metrics.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/request_response_constants.h"
#include "src/core/interface/async_context.h"
#include "src/core/interface/errors.h"
#include "src/public/core/interface/execution_result.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
PeriodicBucketFetcher::PeriodicBucketFetcher(
    absl::string_view bucket_name, absl::Duration fetch_period_ms,
    server_common::Executor* executor, BlobStorageClient* blob_storage_client)
    : bucket_name_(bucket_name),
      fetch_period_ms_(fetch_period_ms),
      executor_(*executor),
      blob_storage_client_(*blob_storage_client) {}

absl::StatusOr<privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>
PeriodicBucketFetcher::ListBlobsSyncParc() {
  privacysandbox::apis::parc::v0::ListBlobsMetadataRequest
      list_blob_metadata_request;
  list_blob_metadata_request.set_exclude_directories(true);
  list_blob_metadata_request.mutable_blob_metadata()->set_bucket_name(
      bucket_name_);

  return ListBlobsMetadataFromResultParc(
      blob_storage_client_.ListBlobsMetadata(list_blob_metadata_request));
}

void PeriodicBucketFetcher::PeriodicBucketFetchSync() {
  absl::StatusOr<privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>
      blob_list = ListBlobsSyncParc();
  if (!blob_list.ok()) {
    PS_LOG(ERROR, SystemLogContext())
        << "Periodic bucket fetch failed for bucket " << bucket_name_
        << ". Will try again in " << fetch_period_ms_ << " milliseconds.";
    task_id_ = executor_.RunAfter(fetch_period_ms_,
                                  [this]() { PeriodicBucketFetchSync(); });
    return;
  } else {
    PS_VLOG(5) << "Will attempt to fetch blobs: " << blob_list->DebugString();
  }

  for (const privacysandbox::apis::parc::v0::BlobMetadata& md :
       blob_list->blob_metadatas()) {
    privacysandbox::apis::parc::v0::GetBlobRequest get_blob_request;
    get_blob_request.mutable_blob_metadata()->set_bucket_name(md.bucket_name());
    get_blob_request.mutable_blob_metadata()->set_blob_name(md.blob_name());
    absl::StatusOr<std::string> get_blob_result =
        GetBlobFromResultParc(blob_storage_client_.GetBlob(get_blob_request));
    absl::MutexLock lock(&some_load_success_mu_);
    if (!get_blob_result.ok()) {
      PS_LOG(ERROR, SystemLogContext())
          << "Fail get blob request for blob "
          << get_blob_request.blob_metadata().blob_name() << " in bucket "
          << GetBucketName();
      continue;
    }
    absl::Status load_status =
        OnFetch(get_blob_request, get_blob_result.value());
    if (!load_status.ok()) {
      PS_LOG(ERROR, SystemLogContext())
          << load_status << " for blob "
          << get_blob_request.blob_metadata().blob_name() << " in bucket "
          << GetBucketName();
    }
    PeriodicBucketFetcherMetrics::UpdateBlobLoadMetrics(
        get_blob_request.blob_metadata().blob_name(), GetBucketName(),
        load_status);
    some_load_success_ = load_status.ok() || some_load_success_;
  }

  PS_VLOG(5) << "Done waiting for blob fetches. Next periodic bucket fetch "
                "will run in "
             << fetch_period_ms_ << " milliseconds.";
  // Schedules the next code blob fetch and saves that task into task_id_.
  task_id_ = executor_.RunAfter(fetch_period_ms_,
                                [this]() { PeriodicBucketFetchSync(); });
}

}  // namespace privacy_sandbox::bidding_auction_servers
