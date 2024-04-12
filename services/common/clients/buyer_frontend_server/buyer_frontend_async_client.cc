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

#include "src/public/cpio/interface/crypto_client/crypto_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

BuyerFrontEndAsyncGrpcClient::BuyerFrontEndAsyncGrpcClient(
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BuyerServiceClientConfig& client_config,
    std::unique_ptr<BuyerFrontEnd::StubInterface> stub)
    : DefaultAsyncGrpcClient(key_fetcher_manager, crypto_client,
                             client_config.cloud_platform),
      stub_(std::move(stub)) {
  if (!stub_) {
    stub_ = BuyerFrontEnd::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  }
}

void BuyerFrontEndAsyncGrpcClient::SendRpc(
    const std::string& hpke_secret,
    RawClientParams<GetBidsRequest, GetBidsResponse,
                    GetBidsResponse::GetBidsRawResponse>* params) const {
  PS_VLOG(5) << "BuyerFrontEndAsyncGrpcClient SendRpc invoked ...";
  stub_->async()->GetBids(
      params->ContextRef(), params->RequestRef(), params->ResponseRef(),
      [this, params, hpke_secret](const grpc::Status& status) {
        if (!status.ok()) {
          PS_VLOG(1) << "SendRPC completion status not ok: "
                     << server_common::ToAbslStatus(status);
          params->OnDone(status);
          return;
        }

        PS_VLOG(6) << "SendRPC completion status ok";
        auto decrypted_response =
            DecryptResponse(hpke_secret, params->ResponseRef());
        if (!decrypted_response.ok()) {
          PS_VLOG(1)
              << "BuyerFrontEndAsyncGrpcClient Failed to decrypt response";
          params->OnDone(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                      decrypted_response.status().ToString()));
          return;
        }

        params->SetRawResponse(*std::move(decrypted_response));
        PS_VLOG(6) << "Returning the decrypted response via callback";
        params->OnDone(status);
      });
}

}  // namespace privacy_sandbox::bidding_auction_servers
