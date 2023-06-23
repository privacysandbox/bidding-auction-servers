//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/common/clients/seller_frontend_server/seller_frontend_async_client.h"

#include <algorithm>

#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {

SellerFrontEndGrpcClient::SellerFrontEndGrpcClient(
    const SellerFrontEndServiceClientConfig& client_config)
    : AsyncClient() {
  std::shared_ptr<grpc::Channel> channel;
  std::shared_ptr<grpc::ChannelCredentials> creds =
      client_config.secure_client
          ? grpc::SslCredentials(grpc::SslCredentialsOptions())
          : grpc::InsecureChannelCredentials();
  if (client_config.compression) {
    grpc::ChannelArguments args;
    // Set the default compression algorithm for the channel.
    args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);
    channel = grpc::CreateCustomChannel(absl::StrCat(client_config.server_addr),
                                        std::move(creds), args);
  } else {
    channel = grpc::CreateChannel(absl::StrCat(client_config.server_addr),
                                  std::move(creds));
  }
  stub_ = SellerFrontEnd::NewStub(channel);
}

absl::Status SellerFrontEndGrpcClient::Execute(
    std::unique_ptr<SelectAdRequest> request, const RequestMetadata& metadata,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<SelectAdResponse>>) &&>
        on_done,
    absl::Duration timeout) const {
  auto params =
      std::make_unique<ClientParams<SelectAdRequest, SelectAdResponse>>(
          std::move(request), std::move(on_done), metadata);
  params->SetDeadline(std::min(sfe_client_max_timeout, timeout));
  stub_->async()->SelectAd(
      params->ContextRef(), params->RequestRef(), params->ResponseRef(),
      [params_ptr = params.release()](grpc::Status status) {
        params_ptr->OnDone(status);
      });
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
