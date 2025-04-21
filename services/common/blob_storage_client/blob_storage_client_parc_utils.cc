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
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"

#include <string>
#include <variant>

#include <apis/privacysandbox/apis/parc/v0/blob.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "services/common/blob_storage_client/blob_storage_client.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<privacysandbox::apis::parc::v0::GetBlobRequest>
GetBlobRequestFromParametersParc(GetBlobParameters get_blob_parameters) {
  if (std::holds_alternative<privacysandbox::apis::parc::v0::GetBlobRequest>(
          get_blob_parameters)) {
    return std::get<privacysandbox::apis::parc::v0::GetBlobRequest>(
        get_blob_parameters);
  }
  return absl::InvalidArgumentError("No valid type from input");
}

absl::StatusOr<std::string> GetBlobFromResultParc(
    GetBlobResponseResult get_blob_result) {
  if (std::holds_alternative<absl::Status>(get_blob_result)) {
    return std::get<absl::Status>(get_blob_result);
  }
  if (std::holds_alternative<std::string>(get_blob_result)) {
    return std::get<std::string>(get_blob_result);
  }
  return absl::InvalidArgumentError("No valid type from input");
}

absl::StatusOr<privacysandbox::apis::parc::v0::ListBlobsMetadataRequest>
ListBlobsMetadataRequestFromParametersParc(
    ListBlobsMetadataParameters list_blobs_metadata_parameters) {
  if (std::holds_alternative<
          privacysandbox::apis::parc::v0::ListBlobsMetadataRequest>(
          list_blobs_metadata_parameters)) {
    return std::get<privacysandbox::apis::parc::v0::ListBlobsMetadataRequest>(
        list_blobs_metadata_parameters);
  }
  return absl::InvalidArgumentError("No valid type from input");
}

absl::StatusOr<privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>
ListBlobsMetadataFromResultParc(
    ListBlobsMetadataResult list_blobs_metadata_result) {
  if (std::holds_alternative<absl::Status>(list_blobs_metadata_result)) {
    return std::get<absl::Status>(list_blobs_metadata_result);
  }
  if (std::holds_alternative<
          privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>(
          list_blobs_metadata_result)) {
    return std::get<privacysandbox::apis::parc::v0::ListBlobsMetadataResponse>(
        list_blobs_metadata_result);
  }
  return absl::InvalidArgumentError("No valid type from input");
}

}  // namespace privacy_sandbox::bidding_auction_servers
