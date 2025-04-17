// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/notification.h"
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/blob_fetch/blob_fetcher_base.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_cpio_utils.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/hash_util.h"
#include "src/core/interface/async_context.h"
#include "src/core/interface/errors.h"
#include "src/public/core/interface/execution_result.h"
#include "src/util/status_macro/status_macros.h"

using ::google::scp::core::AsyncContext;
using ::google::scp::core::ExecutionResult;
using ::google::scp::core::errors::GetErrorMessage;
using ::google::scp::cpio::BlobStorageClientInterface;

namespace privacy_sandbox::bidding_auction_servers {
namespace {
// Checks if the specified path should be included according to the filter
// options. A path is included when either no included prefixes are specified or
// when it matches one of the included prefixes.
bool ShouldIncludePath(absl::string_view path,
                       const BlobFetcher::FilterOptions& filter_options) {
  if (filter_options.included_prefixes.empty()) {
    return true;
  }

  for (const auto& prefix : filter_options.included_prefixes) {
    if (absl::StartsWith(path, prefix)) {
      return true;
    }
  }

  return false;
}

}  // namespace

absl::Status BlobFetcher::InternalFetch(const FilterOptions& filter_options) {
  absl::Status status;
  std::vector<std::string> blob_names;
  std::vector<Blob> new_file_snapshot;

  // List all the blobs in the bucket.
  auto list_blobs_request = std::make_shared<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest>();
  absl::Notification notification;
  list_blobs_request->mutable_blob_metadata()->set_bucket_name(bucket_name_);
  list_blobs_request->set_exclude_directories(true);
  AsyncContext<
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
      google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>
      list_blobs_context(list_blobs_request, [&status, &blob_names,
                                              &notification,
                                              &filter_options](auto& context) {
        if (!context.result.Successful()) {
          PS_LOG(ERROR, SystemLogContext())
              << "Failed to list blobs: "
              << GetErrorMessage(context.result.status_code);
          status = absl::InternalError("Failed to list blobs");
        } else {
          PS_VLOG(10) << "BlobStorageClient ListBlobsMetadata() Response: "
                      << context.response->DebugString();
          for (int i = 0; i < context.response->blob_metadatas_size(); i++) {
            const std::string& blob_name =
                context.response->blob_metadatas(i).blob_name();
            if (ShouldIncludePath(blob_name, filter_options)) {
              blob_names.push_back(blob_name);
            }
          }
        }

        // The caller waits for the notification.
        // Please note that the callback might not be called by
        // ListBlobsMetadata.
        // TODO(b/316960066): Inspect the BlobStorageClient code and fix
        // bugs.
        notification.Notify();
      });

  // If ListBlobsMetadata fails fast, we return the error early without
  // waiting for `notification`.
  PS_RETURN_IF_ERROR(ListBlobsMetadataFromResultCpio(
      blob_storage_client_->ListBlobsMetadata(list_blobs_context)));
  notification.WaitForNotification();

  // Checks the error from the callback.
  PS_RETURN_IF_ERROR(status);

  // Fetches all the blobs in the bucket.
  // TODO(b/329674737): Fetch blobs in parallel.
  for (const std::string& blob_name : blob_names) {
    absl::Notification per_blob_notification;
    auto get_blob_request = std::make_shared<
        google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest>();
    get_blob_request->mutable_blob_metadata()->set_bucket_name(bucket_name_);
    get_blob_request->mutable_blob_metadata()->set_blob_name(blob_name);

    AsyncContext<google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
                 google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>
        get_blob_context(
            get_blob_request, [&status, &new_file_snapshot,
                               &per_blob_notification](auto& context) {
              if (!context.result.Successful()) {
                PS_LOG(ERROR, SystemLogContext())
                    << "Failed to fetch blobs: "
                    << GetErrorMessage(context.result.status_code);
                status = absl::InternalError("Failed to fetch blobs");
              } else {
                // Should not log blob().data(), which can be very large bytes.
                PS_VLOG(10)
                    << "BlobStorageClient GetBlob() Response: "
                    << context.response->blob().metadata().DebugString();

                const std::string& path =
                    context.response->blob().metadata().blob_name();
                const std::string& bytes = context.response->blob().data();
                new_file_snapshot.emplace_back(path, bytes);
              }
              // TODO(b/316960066): Inspect the BlobStorageClient code and fix
              // bugs.
              per_blob_notification.Notify();
            });

    // If GetBlob fails fast, we return the error early. We update the file
    // snapshot only when all the file fetching is successfully done.
    PS_RETURN_IF_ERROR(
        GetBlobFromResultCpio(blob_storage_client_->GetBlob(get_blob_context)));
    per_blob_notification.WaitForNotification();

    // Checks the error from the callback.
    PS_RETURN_IF_ERROR(status);
  }

  // All the blobs are successfully fetched.
  snapshot_ = std::move(new_file_snapshot);
  return status;
}
}  // namespace privacy_sandbox::bidding_auction_servers
