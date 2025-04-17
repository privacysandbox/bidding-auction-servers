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

#include "services/common/blob_storage_client/blob_storage_client_cpio_utils.h"

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
#include "src/public/cpio/proto/blob_storage_service/v1/blob_storage_service.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<
    AsyncContext<google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
                 google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>>
GetBlobRequestFromParametersCpio(GetBlobParameters get_blob_parameters) {
  if (std::holds_alternative<AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
          google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>>(
          get_blob_parameters)) {
    return std::get<AsyncContext<
        google::cmrt::sdk::blob_storage_service::v1::GetBlobRequest,
        google::cmrt::sdk::blob_storage_service::v1::GetBlobResponse>>(
        get_blob_parameters);
  }
  return absl::InvalidArgumentError("No valid type from input");
}

absl::Status GetBlobFromResultCpio(GetBlobResponseResult get_blob_result) {
  if (std::holds_alternative<absl::Status>(get_blob_result)) {
    return std::get<absl::Status>(get_blob_result);
  }
  return absl::OkStatus();
}

absl::StatusOr<AsyncContext<
    google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
    google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataResponse>>
ListBlobsMetadataRequestFromParametersCpio(
    ListBlobsMetadataParameters list_blobs_metadata_parameters) {
  if (std::holds_alternative<AsyncContext<
          google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
          google::cmrt::sdk::blob_storage_service::v1::
              ListBlobsMetadataResponse>>(list_blobs_metadata_parameters)) {
    return std::get<AsyncContext<
        google::cmrt::sdk::blob_storage_service::v1::ListBlobsMetadataRequest,
        google::cmrt::sdk::blob_storage_service::v1::
            ListBlobsMetadataResponse>>(list_blobs_metadata_parameters);
  }
  return absl::InvalidArgumentError("No valid type from input");
}

absl::Status ListBlobsMetadataFromResultCpio(
    ListBlobsMetadataResult list_blobs_metadata_result) {
  if (std::holds_alternative<absl::Status>(list_blobs_metadata_result)) {
    return std::get<absl::Status>(list_blobs_metadata_result);
  }
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
