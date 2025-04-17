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
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/blob_fetch/blob_fetcher_base.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/hash_util.h"
#include "src/util/status_macro/status_macros.h"

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

  privacysandbox::apis::parc::v0::ListBlobsMetadataRequest list_blobs_request;
  list_blobs_request.mutable_blob_metadata()->set_bucket_name(bucket_name_);
  list_blobs_request.set_exclude_directories(true);
  auto list_blobs_response = ListBlobsMetadataFromResultParc(
      blob_storage_client_->ListBlobsMetadata(list_blobs_request));
  if (!list_blobs_response.ok()) {
    PS_LOG(ERROR, SystemLogContext())
        << "Failed to list blobs: " << list_blobs_response.status();
    return list_blobs_response.status();
  }
  // Get blob names from metadata
  PS_VLOG(10) << "BlobStorageClient ListBlobsMetadata() Response: "
              << list_blobs_response.value().DebugString();
  for (int i = 0; i < list_blobs_response.value().blob_metadatas_size(); i++) {
    const std::string& blob_name =
        list_blobs_response.value().blob_metadatas(i).blob_name();
    if (ShouldIncludePath(blob_name, filter_options)) {
      blob_names.push_back(blob_name);
    }
  }
  // get bolb data, and update snapshot.
  for (const std::string& blob_name : blob_names) {
    privacysandbox::apis::parc::v0::GetBlobRequest get_blob_request;
    get_blob_request.mutable_blob_metadata()->set_bucket_name(bucket_name_);
    get_blob_request.mutable_blob_metadata()->set_blob_name(blob_name);
    auto get_blob_response =
        GetBlobFromResultParc(blob_storage_client_->GetBlob(get_blob_request));
    if (!get_blob_response.ok()) {
      PS_LOG(ERROR, SystemLogContext())
          << "Failed to fetch blobs: " << get_blob_response.status();
      return get_blob_response.status();
    }
    // Parc return mereged blob data.
    const std::string& path = blob_name;
    const std::string& bytes = get_blob_response.value();
    new_file_snapshot.emplace_back(path, bytes);
  }
  snapshot_ = std::move(new_file_snapshot);
  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers
