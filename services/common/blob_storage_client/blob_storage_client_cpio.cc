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

#include "services/common/blob_storage_client/blob_storage_client_cpio.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "services/common/blob_storage_client/blob_storage_client_cpio_utils.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

CpioBlobStorageClient::CpioBlobStorageClient(
    std::unique_ptr<google::scp::cpio::BlobStorageClientInterface> client)
    : client_(std::move(client)) {}

absl::Status CpioBlobStorageClient::Init() noexcept { return client_->Init(); }
absl::Status CpioBlobStorageClient::Run() noexcept { return client_->Run(); }
absl::Status CpioBlobStorageClient::Stop() noexcept { return client_->Run(); }

GetBlobResponseResult CpioBlobStorageClient::GetBlob(
    GetBlobParameters get_blob_parameters) noexcept {
  // Get paratemeter for CPIO
  auto get_res = GetBlobRequestFromParametersCpio(get_blob_parameters);
  if (!get_res.ok()) {
    return get_res.status();
  }
  if (absl::Status status = client_->GetBlob(get_res.value()); !status.ok()) {
    return status;
  }
  return absl::OkStatus();
}

ListBlobsMetadataResult CpioBlobStorageClient::ListBlobsMetadata(
    ListBlobsMetadataParameters list_blobs_metadata_parameters) noexcept {
  // Get paratemeter for CPIO
  auto get_res = ListBlobsMetadataRequestFromParametersCpio(
      list_blobs_metadata_parameters);
  if (!get_res.ok()) {
    return get_res.status();
  }
  if (absl::Status status = client_->ListBlobsMetadata(get_res.value());
      !status.ok()) {
    return status;
  }
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
