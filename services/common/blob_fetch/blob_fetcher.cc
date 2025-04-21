// Copyright 2024 Google LLC
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

#include "services/common/blob_fetch/blob_fetcher.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/notification.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_cpio_utils.h"
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"
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

BlobFetcher::BlobFetcher(absl::string_view bucket_name,
                         server_common::Executor* executor,
                         std::unique_ptr<BlobStorageClient> blob_storage_client)
    : bucket_name_(bucket_name),
      executor_(executor),
      blob_storage_client_(std::move(blob_storage_client)) {
  PS_CHECK_OK(blob_storage_client_->Init(), SystemLogContext())
      << "Failed to init BlobStorageClient;";
  PS_CHECK_OK(blob_storage_client_->Run(), SystemLogContext())
      << "Failed to run BlobStorageClient;";
}

absl::Status BlobFetcher::FetchSync(const FilterOptions& filter_options) {
  absl::Status status;
  absl::Notification done;
  executor_->Run([&status, &done, &filter_options, this]() {
    status = InternalFetch(filter_options);
    done.Notify();
  });
  done.WaitForNotification();
  return status;
}

absl::StatusOr<std::string> ComputeChecksumForBlobs(
    const std::vector<BlobFetcherBase::BlobView>& blob_views) {
  if (blob_views.empty()) {
    return absl::InvalidArgumentError("Can't compute checksum for empty blobs");
  }

  std::vector<std::pair<std::string, std::string>> blob_hashes;
  blob_hashes.reserve(blob_views.size());
  for (const BlobFetcherBase::BlobView& blob_view : blob_views) {
    blob_hashes.emplace_back(blob_view.path, ComputeSHA256(blob_view.bytes));
  }

  std::sort(blob_hashes.begin(), blob_hashes.end());

  std::string top_hash;
  for (const auto& blob_hash : blob_hashes) {
    top_hash += blob_hash.second;
  }

  return ComputeSHA256(top_hash);
}

}  // namespace privacy_sandbox::bidding_auction_servers
