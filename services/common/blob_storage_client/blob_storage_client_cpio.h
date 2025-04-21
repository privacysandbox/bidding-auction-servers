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
#ifndef SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_CPIO_H_
#define SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_CPIO_H_

#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class CpioBlobStorageClient : public BlobStorageClient {
 public:
  explicit CpioBlobStorageClient(
      std::unique_ptr<google::scp::cpio::BlobStorageClientInterface> client);

  ~CpioBlobStorageClient() = default;

  absl::Status Init() noexcept override;
  absl::Status Run() noexcept override;
  absl::Status Stop() noexcept override;

  GetBlobResponseResult GetBlob(
      GetBlobParameters get_blob_parameters) noexcept override;

  ListBlobsMetadataResult ListBlobsMetadata(
      ListBlobsMetadataParameters list_blobs_metadata_parameter) noexcept
      override;

  std::unique_ptr<google::scp::cpio::BlobStorageClientInterface> client_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_CPIO_H_
