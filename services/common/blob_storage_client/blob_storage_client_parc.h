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
#ifndef SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_PARC_H_
#define SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_PARC_H_

#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <grpcpp/grpcpp.h>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>
#include <apis/privacysandbox/apis/parc/v0/parc_service.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "src/core/interface/async_context.h"

namespace privacy_sandbox::bidding_auction_servers {

class ParcBlobStorageClient : public BlobStorageClient {
 public:
  explicit ParcBlobStorageClient(
      std::shared_ptr<
          privacysandbox::apis::parc::v0::ParcService::StubInterface>
          client_stub);

  ~ParcBlobStorageClient() = default;

  absl::Status Init() noexcept override;
  absl::Status Run() noexcept override;
  absl::Status Stop() noexcept override;

  // PARC Getblob is Sync and blocking before stream rpc finish.
  GetBlobResponseResult GetBlob(
      GetBlobParameters get_blob_parameters) noexcept override;

  // PARC ListBlobsMetadata is Sync and blocking before all mertadata are
  // fetched.
  ListBlobsMetadataResult ListBlobsMetadata(
      ListBlobsMetadataParameters list_blobs_metadata_parameters) noexcept
      override;

  std::shared_ptr<privacysandbox::apis::parc::v0::ParcService::StubInterface>
      client_stub_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_BLOB_STORAGE_CLIENT_BLOB_STORAGE_CLIENT_PARC_H_
