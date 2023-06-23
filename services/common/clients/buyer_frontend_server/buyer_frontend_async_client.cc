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

#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"

#include "cc/public/cpio/interface/crypto_client/crypto_client_interface.h"
#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

BuyerFrontEndAsyncGrpcClient::BuyerFrontEndAsyncGrpcClient(
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    BuyerServiceClientConfig client_config)
    : DefaultAsyncGrpcClient(std::move(key_fetcher_manager),
                             std::move(crypto_client),
                             client_config.encryption_enabled) {
  stub_ = BuyerFrontEnd::NewStub(CreateChannel(client_config.server_addr,
                                               client_config.compression,
                                               client_config.secure_client));
}

absl::Status BuyerFrontEndAsyncGrpcClient::SendRpc(
    ClientParams<GetBidsRequest, GetBidsResponse>* params,
    absl::string_view hpke_secret) const {
  stub_->async()->GetBids(
      params->ContextRef(), params->RequestRef(), params->ResponseRef(),
      [this, params, hpke_secret](grpc::Status status) {
        if (encryption_enabled_ &&
            !DecryptResponse(params->ResponseRef(), hpke_secret).ok()) {
          status = grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed decrypting Buyer Service response");
        }

        params->OnDone(status);
      });

  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
