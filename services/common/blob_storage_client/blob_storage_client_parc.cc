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

#include "services/common/blob_storage_client/blob_storage_client_parc.h"

#include <utility>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc_utils.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

ParcBlobStorageClient::ParcBlobStorageClient(
    std::shared_ptr<privacysandbox::apis::parc::v0::ParcService::StubInterface>
        client_stub)
    : client_stub_(std::move(client_stub)) {}

// Only need support for cpio client, parc do not required.
absl::Status ParcBlobStorageClient::Init() noexcept { return absl::OkStatus(); }
absl::Status ParcBlobStorageClient::Run() noexcept { return absl::OkStatus(); }
absl::Status ParcBlobStorageClient::Stop() noexcept { return absl::OkStatus(); }

GetBlobResponseResult ParcBlobStorageClient::GetBlob(
    GetBlobParameters get_blob_parameters) noexcept {
  // Get paratemeter for PARC
  auto get_res = GetBlobRequestFromParametersParc(get_blob_parameters);
  privacysandbox::apis::parc::v0::GetBlobRequest parc_request;
  if (!get_res.ok()) {
    return get_res.status();
  } else {
    parc_request = get_res.value();
  }
  grpc::ClientContext context;

  // TODO: review if we need retry logic.
  std::unique_ptr<grpc::ClientReaderInterface<
      privacysandbox::apis::parc::v0::GetBlobResponse>>
      reader = client_stub_->GetBlob(&context, parc_request);
  std::string data_response;
  privacysandbox::apis::parc::v0::GetBlobResponse parc_response;

  // PARC's GetBlob is server stream Rpc, to keep logic consist for B&A we will
  // merge response into single std::string.
  // TODO: consider using other efficient copy method, eg:riegeli::StringWriter
  while (reader->Read(&parc_response)) {
    // Copy over metadata. Return new mergered blob data.
    absl::StrAppend(&data_response, parc_response.data());
  }
  if (grpc::Status status = reader->Finish(); !status.ok()) {
    return privacy_sandbox::server_common::ToAbslStatus(status);
  }

  return data_response;
};

ListBlobsMetadataResult ParcBlobStorageClient::ListBlobsMetadata(
    ListBlobsMetadataParameters list_blobs_metadata_parameters) noexcept {
  // Get paratemeter for PARC
  privacysandbox::apis::parc::v0::ListBlobsMetadataRequest parc_request;
  auto get_res = ListBlobsMetadataRequestFromParametersParc(
      list_blobs_metadata_parameters);
  if (!get_res.ok()) {
    return get_res.status();
  } else {
    parc_request = get_res.value();
  }
  privacysandbox::apis::parc::v0::ListBlobsMetadataResponse merged_response;
  bool done = false;

  while (!done) {
    grpc::ClientContext context;
    privacysandbox::apis::parc::v0::ListBlobsMetadataResponse parc_response;
    // If any ListBlobsMetadata call fails, we return the error early.
    if (grpc::Status error = client_stub_->ListBlobsMetadata(
            &context, parc_request, &parc_response);
        !error.ok()) {
      return privacy_sandbox::server_common::ToAbslStatus(error);
    }

    for (const privacysandbox::apis::parc::v0::BlobMetadata& blob_metadata :
         *parc_response.mutable_blob_metadatas()) {
      *merged_response.add_blob_metadatas() = blob_metadata;
    }
    if (!parc_response.next_page_token().empty()) {
      parc_request.set_page_token(parc_response.next_page_token());
    } else {
      done = true;
    }
    if (parc_response.blob_metadatas().empty()) {
      done = true;
    }
  }
  return merged_response;
};

}  // namespace privacy_sandbox::bidding_auction_servers
