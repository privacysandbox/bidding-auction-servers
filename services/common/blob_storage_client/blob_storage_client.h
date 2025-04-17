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

#ifndef SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_H_
#define SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_H_

#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <apis/privacysandbox/apis/parc/v0/blob.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/core/interface/async_context.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"
#include "src/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::scp::core::AsyncContext;
using GetBlobParameters = std::variant<
    AsyncContext<google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
                 google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>,
    privacysandbox::apis::parc::v0::GetBlobRequest>;
// Using std::string for PARC return value to avoid proto max size limit.
using GetBlobResponseResult = std::variant<absl::Status, std::string>;
using ListBlobsMetadataParameters = std::variant<
    AsyncContext<
        google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
        google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>,
    privacysandbox::apis::parc::v0::ListBlobsMetadataRequest>;
using ListBlobsMetadataResult =
    std::variant<absl::Status,
                 privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>;

class BlobStorageClient {
 public:
  virtual ~BlobStorageClient() = default;

  virtual absl::Status Init() noexcept = 0;
  virtual absl::Status Run() noexcept = 0;
  virtual absl::Status Stop() noexcept = 0;

  virtual GetBlobResponseResult GetBlob(
      GetBlobParameters get_blob_parameters) noexcept = 0;

  virtual ListBlobsMetadataResult ListBlobsMetadata(
      ListBlobsMetadataParameters list_blobs_metadata_parameters) noexcept = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_H_
