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

#include "services/common/code_fetch/periodic_bucket_fetcher.h"

#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/time/time.h"
#include "services/common/code_fetch/code_fetcher_interface.h"
#include "src/core/interface/async_context.h"
#include "src/core/interface/errors.h"
#include "src/logger/request_context_logger.h"
#include "src/public/core/interface/execution_result.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"
#include "src/util/status_macro/status_macros.h"

using ::google::cmrt::sdk::blob_storage_service::v1::BlobMetadata;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest;
using ::google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse;
using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::BlobStorageClientFactory;
using ::google::scp::cpio::BlobStorageClientInterface;

namespace privacy_sandbox::bidding_auction_servers {

PeriodicBucketFetcher::PeriodicBucketFetcher(
    absl::string_view bucket_name, absl::Duration fetch_period_ms,
    V8Dispatcher* dispatcher, server_common::Executor* executor,
    WrapCodeForDispatch wrap_code,
    BlobStorageClientInterface* blob_storage_client)
    : bucket_name_(bucket_name),
      wrap_code_(std::move(wrap_code)),
      fetch_period_ms_(fetch_period_ms),
      dispatcher_(*dispatcher),
      executor_(*executor),
      blob_storage_client_(*blob_storage_client) {}

absl::Status PeriodicBucketFetcher::Start() {
  PeriodicBucketFetchSync();
  absl::MutexLock lock(&some_load_success_mu_);
  if (some_load_success_) {
    return absl::OkStatus();
  } else {
    return absl::InternalError("No code blob loaded successfully.");
  }
}

void PeriodicBucketFetcher::End() {
  if (task_id_.has_value()) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

absl::StatusOr<ListBlobsMetadataResponse>
PeriodicBucketFetcher::ListBlobsSync() {
  absl::StatusOr<ListBlobsMetadataResponse> result;
  absl::Notification notification;
  auto list_blobs_request = std::make_shared<ListBlobsMetadataRequest>();
  list_blobs_request->mutable_blob_metadata()->set_bucket_name(bucket_name_);

  AsyncContext<ListBlobsMetadataRequest, ListBlobsMetadataResponse>
      list_blobs_context(
          list_blobs_request, [&notification, &result](auto& context) {
            if (!context.result.Successful()) {
              std::string error_msg =
                  absl::StrCat("Failed to list available blobs: ",
                               GetErrorMessage(context.result.status_code));
              result = absl::InternalError(std::move(error_msg));
            } else {
              // Copy the response:
              result = std::move(*(context.response));
            }
            PS_VLOG(5) << "Notifying of list blob metadata success status: "
                       << context.result.Successful();
            notification.Notify();
          });

  auto list_blobs_execution =
      blob_storage_client_.ListBlobsMetadata(list_blobs_context);
  if (!list_blobs_execution.Successful()) {
    result = absl::InternalError(
        absl::StrCat("ListBlobsMetadata attempt failed: ",
                     GetErrorMessage(list_blobs_execution.status_code)));
    PS_VLOG(5) << "List blob metadata failed.";
  } else {
    PS_VLOG(5) << "Waiting for list blob metadata done notification.";
    notification.WaitForNotification();
    PS_VLOG(5) << "List blob metadata wait finished.";
  }
  return result;
}

void PeriodicBucketFetcher::HandleBlobFetchResult(
    const AsyncContext<GetBlobRequest, GetBlobResponse>& context) {
  absl::string_view version = context.request->blob_metadata().blob_name();
  if (!context.result.Successful()) {
    PS_VLOG(0) << "Failed to fetch blob: " << version
               << GetErrorMessage(context.result.status_code);
    return;
  }
  auto result_value = {context.response->blob().data()};
  std::string wrapped_code = wrap_code_(result_value);
  absl::Status roma_result = dispatcher_.LoadSync(version, wrapped_code);
  if (!roma_result.ok()) {
    PS_VLOG(0) << "Roma failed to load blob: " << roma_result;
    return;
  }
  PS_VLOG(1) << "Current code loaded into Roma for version " << version << ":\n"
             << wrapped_code;
  absl::MutexLock lock(&some_load_success_mu_);
  some_load_success_ = true;
}

void PeriodicBucketFetcher::PeriodicBucketFetchSync() {
  absl::StatusOr<ListBlobsMetadataResponse> blob_list = ListBlobsSync();
  if (!blob_list.ok()) {
    PS_VLOG(0) << "Periodic bucket fetch failed for bucket " << bucket_name_
               << ". Will try again in " << fetch_period_ms_
               << " milliseconds.";
    task_id_ = executor_.RunAfter(fetch_period_ms_,
                                  [this]() { PeriodicBucketFetchSync(); });
    return;
  }

  // TODO: We must evict any versions in Roma but not in the bucket. We
  // should also only fetch blobs if their metadata indicates a change.

  absl::BlockingCounter blobs_remaining(blob_list->blob_metadatas_size());

  for (const BlobMetadata& md : blob_list->blob_metadatas()) {
    auto get_blob_request = std::make_shared<GetBlobRequest>();
    get_blob_request->mutable_blob_metadata()->set_bucket_name(
        md.bucket_name());
    get_blob_request->mutable_blob_metadata()->set_blob_name(md.blob_name());
    AsyncContext<GetBlobRequest, GetBlobResponse> get_blob_context(
        get_blob_request, [&blobs_remaining, this](const auto& context) {
          HandleBlobFetchResult(context);
          blobs_remaining.DecrementCount();
        });

    auto get_blob_result = blob_storage_client_.GetBlob(get_blob_context);

    if (!get_blob_result.Successful()) {
      blobs_remaining.DecrementCount();
      PS_VLOG(0) << "GetBlob attempt failed for " << md.blob_name()
                 << GetErrorMessage(get_blob_result.status_code);
    }
  }

  PS_VLOG(5) << "Waiting for blob fetches to complete.";
  blobs_remaining.Wait();
  PS_VLOG(5) << "Done waiting for blob fetches. Next periodic bucket fetch "
                "will run in "
             << fetch_period_ms_ << " milliseconds.";
  // Schedules the next code blob fetch and saves that task into task_id_.
  task_id_ = executor_.RunAfter(fetch_period_ms_,
                                [this]() { PeriodicBucketFetchSync(); });
}

}  // namespace privacy_sandbox::bidding_auction_servers
