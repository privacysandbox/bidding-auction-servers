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

#include <algorithm>

#include "services/common/chaffing/transcoding_utils.h"
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
      stub_(std::move(stub)),
      chaffing_enabled_(client_config.chaffing_enabled) {
  if (!stub_) {
    stub_ = BuyerFrontEnd::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  }
}

absl::Status BuyerFrontEndAsyncGrpcClient::ExecuteInternal(
    std::unique_ptr<GetBidsRequest::GetBidsRawRequest> raw_request,
    const RequestMetadata& metadata,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>,
                            ResponseMetadata) &&>
        on_done,
    absl::Duration timeout, RequestConfig request_config) const {
  if (!chaffing_enabled_) {
    return DefaultAsyncGrpcClient::ExecuteInternal(std::move(raw_request),
                                                   metadata, std::move(on_done),
                                                   timeout, request_config);
  }

  // If chaffing is enabled, we encode requests according to the new request
  // format.
  PS_VLOG(6) << "Raw request:\n" << raw_request->DebugString();
  std::string encoded_req_payload =
      EncodeGetBidsPayload(*raw_request, request_config.chaff_request_size);

  return EncryptPayloadAndSendRpc(encoded_req_payload, metadata,
                                  std::move(on_done), timeout, request_config);
}

void BuyerFrontEndAsyncGrpcClient::OnGetBidsDoneChaffingDisabled(
    absl::string_view decrypted_payload, const grpc::Status& status,
    BuyerFrontendRawClientParams* params) const {
  std::unique_ptr<GetBidsResponse::GetBidsRawResponse> raw_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  if (!raw_response->ParseFromString(decrypted_payload)) {
    PS_LOG(ERROR) << "Failed to parse proto from decrypted response";
    params->OnDone(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                raw_response->DebugString()));
    return;
  }

  PS_VLOG(6) << "Decryption/decoding of response succeeded: "
             << raw_response->DebugString();

  params->SetRawResponse(std::move(raw_response));
  PS_VLOG(6) << "Returning the decrypted response via callback";
  params->OnDone(status);
}

void BuyerFrontEndAsyncGrpcClient::OnGetBidsDoneChaffingEnabled(
    absl::string_view decrypted_payload, const grpc::Status& status,
    BuyerFrontendRawClientParams* params) const {
  if (params->RequestConfig().chaff_request_size > 0) {
    // If the request was chaff, return an empty response up to
    // select_ad_reactor regardless of the actual response from the BFE.
    params->SetRawResponse(
        std::make_unique<GetBidsResponse::GetBidsRawResponse>());
    params->OnDone(grpc::Status());  // Defaults to 'StatusCode::OK'.
    return;
  }

  std::unique_ptr<GetBidsResponse::GetBidsRawResponse> raw_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();
  // If the request wasn't chaff, the response will have no padding. So
  // everything past the metadata bytes are the actual payload.
  absl::string_view payload(
      &decrypted_payload[kTotalMetadataSizeBytes],
      decrypted_payload.length() - kTotalMetadataSizeBytes);
  if (!raw_response->ParseFromString(payload)) {
    PS_LOG(ERROR) << "Failed to parse proto from decrypted response: "
                  << decrypted_payload;
    params->OnDone(
        grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                     "Failed to parse proto from decrypted response"));
    return;
  }

  params->SetRawResponse(std::move(raw_response));
  PS_VLOG(6) << "Returning the decrypted response via callback";
  params->OnDone(status);
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
          PS_LOG(ERROR) << "SendRPC completion status not ok: "
                        << server_common::ToAbslStatus(status);
          params->OnDone(status);
          return;
        }

        // For metric purposes in the reactor, note the response size.
        params->SetResponseMetadata(
            {.response_size = params->ResponseRef()->ByteSizeLong()});

        PS_VLOG(6) << "Decrypting the response ...";
        absl::StatusOr<
            google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
            decrypt_response = crypto_client_->AeadDecrypt(
                params->ResponseRef()->response_ciphertext(), hpke_secret);
        if (!decrypt_response.ok()) {
          PS_LOG(ERROR)
              << "BuyerFrontEndAsyncGrpcClient Failed to decrypt response";
          params->OnDone(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                      decrypt_response.status().ToString()));
        }

        absl::string_view decrypted_payload = decrypt_response->payload();
        // Look at the first byte of the decrypted payload to determine whether
        // the BFE responded in the new or old request format. In the old
        // format, the decrypted payload is just a serialized proto message,
        // which will never have the null terminator as the first character. In
        // the new format, the decrypted payload's first byte is guaranteed to
        // be zero for now (it will be non-zero when we change the version
        // number or enable compression).
        bool is_new_format_bfe_response =
            !decrypted_payload.empty() && decrypted_payload.front() == '\0';

        if (is_new_format_bfe_response && chaffing_enabled_) {
          OnGetBidsDoneChaffingEnabled(decrypted_payload, status, params);
        } else {
          OnGetBidsDoneChaffingDisabled(decrypted_payload, status, params);
        }
      });
}

}  // namespace privacy_sandbox::bidding_auction_servers
