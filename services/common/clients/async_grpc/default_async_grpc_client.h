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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_ASYNC_GRPC_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_ASYNC_GRPC_CLIENT_H_

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "glog/logging.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/client_params.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/util/status_macros.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

// This can be made configurable
inline constexpr absl::Duration max_timeout = absl::Milliseconds(60000);

// This class acts as a template for a basic asynchronous grpc client.
template <typename Request, typename Response, typename RawResponse>
class DefaultAsyncGrpcClient : public AsyncClient<Request, Response> {
 public:
  DefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client, bool encryption_enabled)
      : AsyncClient<Request, Response>(),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client),
        encryption_enabled_(encryption_enabled) {}

  DefaultAsyncGrpcClient(const DefaultAsyncGrpcClient&) = delete;
  DefaultAsyncGrpcClient& operator=(const DefaultAsyncGrpcClient&) = delete;

  // Executes the grpc request asynchronously.
  //
  // request: the request object to execute
  // metadata: Metadata to be passed to the client
  // on_done: callback called when the request is finished executing
  // timeout: a timeout value for the request
  // TODO(b/285963550): Handle the sync failures in reactor classes.
  absl::Status Execute(
      std::unique_ptr<Request> request, const RequestMetadata& metadata,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Response>>) &&>
          on_done,
      absl::Duration timeout = max_timeout) const override {
    std::string hpke_secret;
    if (encryption_enabled_) {
      PS_ASSIGN_OR_RETURN(hpke_secret, EncryptRequest(request.get()));
    }

    auto params = std::make_unique<ClientParams<Request, Response>>(
        std::move(request), std::move(on_done), metadata);
    params->SetDeadline(std::min(max_timeout, timeout));
    return SendRpc(params.release(), hpke_secret);
  }

 protected:
  // Sends an asynchronous request via grpc. This method must be implemented
  // by classes implementing this interface.
  //
  // params: a pointer to the ClientParams object which carries data used
  // by the grpc stub.
  // hpke_secret: secret generated during HPKE encryption used during
  // AeadDecryption
  virtual absl::Status SendRpc(ClientParams<Request, Response>* params,
                               absl::string_view hpke_secret) const = 0;

  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  CryptoClientWrapperInterface* crypto_client_;

  // Whether HPKE encryption is enabled for intra-server communication.
  bool encryption_enabled_;

  absl::StatusOr<std::string> EncryptRequest(Request* request) const {
    absl::StatusOr<PublicKey> key = key_fetcher_manager_->GetPublicKey();
    if (!key.ok()) {
      const std::string error =
          absl::StrCat("Could not get public key to use for HPKE encryption: ",
                       key.status().message());
      LOG(ERROR) << error;
      return absl::InternalError(error);
    }

    auto encrypt_response = crypto_client_->HpkeEncrypt(
        key.value(), request->raw_request().SerializeAsString());

    if (!encrypt_response.ok()) {
      const std::string error = absl::StrCat(
          "Failed encrypting request: ", encrypt_response.status().message());
      LOG(ERROR) << error;
      return absl::InternalError(error);
    }

    request->set_key_id(key->key_id());
    request->set_request_ciphertext(
        std::move(encrypt_response->encrypted_data().ciphertext()));
    request->clear_raw_request();
    return std::move(encrypt_response->secret());
  }

  absl::Status DecryptResponse(Response* response,
                               absl::string_view hpke_secret) const {
    absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
        decrypt_response = crypto_client_->AeadDecrypt(
            response->response_ciphertext(), hpke_secret);
    if (!decrypt_response.ok()) {
      const std::string error = absl::StrCat(
          "Could not decrypt response: ", decrypt_response.status().message());
      LOG(ERROR) << error;
      return absl::InternalError(error);
    }

    RawResponse raw_response;
    raw_response.ParseFromString(decrypt_response->payload());
    *response->mutable_raw_response() = std::move(raw_response);
    response->clear_response_ciphertext();
    return absl::OkStatus();
  }
};

// Creates a shared grpc channel from a given server URL. This channel
// is passed into stubs owned by specific implementations of gRPC clients
// since the stub type is based on the service.
// server_addr: the URL or IP for the server DNS
// compression: flag to enable gRPC level compression for the client.
// Disabled by default.
inline std::shared_ptr<grpc::Channel> CreateChannel(
    // Const string reference to prevent copies. string_view cannot be casted
    // to argument for grpc::CreateChannel.
    absl::string_view server_addr, bool compression = false,
    bool secure = true) {
  std::shared_ptr<grpc::Channel> channel;
  std::shared_ptr<grpc::ChannelCredentials> creds =
      secure ? grpc::SslCredentials(grpc::SslCredentialsOptions())
             : grpc::InsecureChannelCredentials();
  if (compression) {
    grpc::ChannelArguments args;
    // Set the default compression algorithm for the channel.
    args.SetCompressionAlgorithm(GRPC_COMPRESS_GZIP);
    channel =
        grpc::CreateCustomChannel(server_addr.data(), std::move(creds), args);
  } else {
    channel = grpc::CreateChannel(server_addr.data(), std::move(creds));
  }
  return channel;
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_ASYNC_GRPC_CLIENT_H_
