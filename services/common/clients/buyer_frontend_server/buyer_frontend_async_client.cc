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
#include "services/common/compression/compression_utils.h"
#include "services/common/util/request_response_constants.h"
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
    grpc::ClientContext* context,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<
                                GetBidsResponse::GetBidsRawResponse>>,
                            ResponseMetadata) &&>
        on_done,
    absl::Duration timeout, RequestConfig request_config) {
  PS_VLOG(kOriginated) << "Raw request:\n" << raw_request->DebugString();
  PS_VLOG(kStats) << "Request size before compression: "
                  << raw_request->SerializeAsString().length();
  if (!chaffing_enabled_) {
    PS_VLOG(kNoisyInfo) << "Encrypting request ...";

    // Chaffing, and therefore the new request/response format, is disabled.
    CompressionType compression_type = request_config.compression_type;
    PS_ASSIGN_OR_RETURN(
        std::string encoded_req_payload,
        Compress(raw_request->SerializeAsString(), compression_type));

    PS_VLOG(kStats) << "Request size after compression: "
                    << encoded_req_payload.length();
    PS_VLOG(kStats) << "Compression type: "
                    << static_cast<int>(compression_type);

    return EncryptPayloadAndSendRpc(encoded_req_payload, context,
                                    std::move(on_done), timeout,
                                    request_config);
  }

  // If chaffing is enabled, we encode requests according to the new request
  // format.
  PS_ASSIGN_OR_RETURN(std::string encoded_req_payload,
                      EncodeAndCompressGetBidsPayload(
                          *raw_request, request_config.compression_type,
                          request_config.chaff_request_size));
  PS_VLOG(kStats) << "Request size after compression: "
                  << encoded_req_payload.length();
  PS_VLOG(kStats) << "compression_type: "
                  << static_cast<int>(request_config.compression_type);

  return EncryptPayloadAndSendRpc(encoded_req_payload, context,
                                  std::move(on_done), timeout, request_config);
}

void BuyerFrontEndAsyncGrpcClient::OnGetBidsDoneChaffingDisabled(
    std::string decrypted_payload, const grpc::Status& status,
    BuyerFrontendRawClientParams* params) const {
  PS_VLOG(8) << "In OnGetBidsDoneChaffingDisabled()";
  std::unique_ptr<GetBidsResponse::GetBidsRawResponse> raw_response =
      std::make_unique<GetBidsResponse::GetBidsRawResponse>();

  CompressionType compression_type = params->RequestConfig().compression_type;
  PS_VLOG(kStats) << "compression_type: " << static_cast<int>(compression_type);
  PS_VLOG(kStats) << "Decrypted response size (before decompression): "
                  << decrypted_payload.length();

  absl::StatusOr<std::string> decompressed =
      Decompress(std::move(decrypted_payload), compression_type);
  if (!decompressed.ok()) {
    PS_LOG(ERROR) << "Failed to decompress GetBids response: "
                  << decompressed.status();
    params->OnDone(grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to decompress GetBids response"));
    return;
  }

  PS_VLOG(kStats) << "Decompressed response size: " << decompressed->length();
  if (!raw_response->ParseFromString(*decompressed)) {
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
    std::string decrypted_payload, const grpc::Status& status,
    BuyerFrontendRawClientParams* params) const {
  PS_VLOG(9) << "In OnGetBidsDoneChaffingEnabled()";
  if (params->RequestConfig().chaff_request_size > 0) {
    PS_VLOG(9) << "Request was chaff, ignoring response";
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
  PS_VLOG(kStats) << "Decrypted payload length: " << payload.length();

  absl::StatusOr<std::string> decompressed =
      Decompress(params->RequestConfig().compression_type, payload);

  if (!decompressed.ok()) {
    PS_LOG(ERROR) << "Failed to decompress GetBids response: "
                  << decompressed.status();
    params->OnDone(grpc::Status(grpc::StatusCode::INTERNAL,
                                "Failed to decompress GetBids response"));
    return;
  }

  PS_VLOG(kStats) << "Decompressed payload length: " << decompressed->length();
  if (!raw_response->ParseFromString(*decompressed)) {
    PS_LOG(ERROR) << "Failed to parse proto from decrypted response: "
                  << decrypted_payload;
    params->OnDone(
        grpc::Status(grpc::StatusCode::INTERNAL,
                     "Failed to parse proto from decrypted response"));
    return;
  }

  params->SetRawResponse(std::move(raw_response));
  PS_VLOG(6) << "Returning the decrypted response via callback";
  params->OnDone(status);
}

void BuyerFrontEndAsyncGrpcClient::SendRpc(
    const std::string& hpke_secret, grpc::ClientContext* context,
    RawClientParams<GetBidsRequest, GetBidsResponse,
                    GetBidsResponse::GetBidsRawResponse>* params) const {
  PS_VLOG(5) << "BuyerFrontEndAsyncGrpcClient SendRpc invoked ...";
  stub_->async()->GetBids(
      context, params->RequestRef(), params->ResponseRef(),
      [this, params, hpke_secret](const grpc::Status& status) {
        if (!status.ok()) {
          PS_LOG(ERROR) << "SendRPC completion status not ok: "
                        << server_common::ToAbslStatus(status);
          params->OnDone(status);
          return;
        }

        // For metric purposes in the reactor, note the request and response
        // size.
        params->SetResponseMetadata(
            {.request_size = params->RequestRef()->ByteSizeLong(),
             .response_size = params->ResponseRef()->ByteSizeLong()});

        PS_VLOG(6) << "Decrypting the response ...";
        absl::StatusOr<
            google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
            decrypt_response = crypto_client_->AeadDecrypt(
                params->ResponseRef()->response_ciphertext(), hpke_secret);
        if (!decrypt_response.ok()) {
          PS_LOG(ERROR, SystemLogContext())
              << "BuyerFrontEndAsyncGrpcClient Failed to decrypt response";
          params->OnDone(grpc::Status(grpc::StatusCode::INTERNAL,
                                      decrypt_response.status().ToString()));
        }

        std::string decrypted_payload =
            std::move(*decrypt_response->mutable_payload());
        if (chaffing_enabled_) {
          OnGetBidsDoneChaffingEnabled(std::move(decrypted_payload), status,
                                       params);
        } else {
          // If chaffing is disabled, thtis is a old format + compressed
          // response.
          OnGetBidsDoneChaffingDisabled(std::move(decrypted_payload), status,
                                        params);
        }
      });
}

}  // namespace privacy_sandbox::bidding_auction_servers
