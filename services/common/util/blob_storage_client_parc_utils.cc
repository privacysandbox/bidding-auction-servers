// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>

#include "absl/flags/flag.h"
#include "services/common/blob_storage_client/blob_storage_client.h"
#include "services/common/blob_storage_client/blob_storage_client_parc.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/util/blob_storage_client_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

std::unique_ptr<BlobStorageClient> BuildBlobStorageClient() {
  // Assume we always provide parc_server_address when Build flag enable_parc is
  // set.
  std::optional<std::string> parc_server_address =
      absl::GetFlag(FLAGS_parc_addr);
  // 8 MB, can be made configurable.
  grpc::ChannelArguments grpc_channel_arguments;
  // Set max message receive size to 12 MiB.
  grpc_channel_arguments.SetMaxReceiveMessageSize(12 * 1 << 20);
  std::shared_ptr<grpc::Channel> grpc_channel = grpc::CreateCustomChannel(
      parc_server_address.value(), grpc::InsecureChannelCredentials(),
      grpc_channel_arguments);

  return std::make_unique<ParcBlobStorageClient>(
      privacysandbox::apis::parc::v0::ParcService::NewStub(grpc_channel));
}
}  // namespace privacy_sandbox::bidding_auction_servers
