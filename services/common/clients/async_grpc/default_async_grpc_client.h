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
#include "services/common/clients/async_grpc/grpc_client_utils.h"
#include "services/common/clients/client_params.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/util/error_categories.h"
#include "services/common/util/status_macros.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

// This can be made configurable
inline constexpr absl::Duration max_timeout = absl::Milliseconds(60000);

// This class acts as a template for a basic asynchronous grpc client.
template <typename Request, typename Response, typename RawRequest,
          typename RawResponse>
class DefaultAsyncGrpcClient
    : public AsyncClient<Request, Response, RawRequest, RawResponse> {
 public:
  DefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client, bool encryption_enabled)
      : AsyncClient<Request, Response, RawRequest, RawResponse>(),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client),
        encryption_enabled_(encryption_enabled) {
#if defined(CLOUD_PLATFORM_AWS)
    cloud_platform_ = server_common::CloudPlatform::AWS;
#elif defined(CLOUD_PLATFORM_GCP)
    cloud_platform_ = server_common::CloudPlatform::GCP;
#else
    cloud_platform_ = server_common::CloudPlatform::LOCAL;
#endif
  }

  DefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client, bool encryption_enabled,
      server_common::CloudPlatform cloud_platform)
      : AsyncClient<Request, Response, RawRequest, RawResponse>(),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client),
        encryption_enabled_(encryption_enabled),
        cloud_platform_(cloud_platform) {}

  DefaultAsyncGrpcClient(const DefaultAsyncGrpcClient&) = delete;
  DefaultAsyncGrpcClient& operator=(const DefaultAsyncGrpcClient&) = delete;

  absl::Status ExecuteInternal(
      std::unique_ptr<RawRequest> raw_request, const RequestMetadata& metadata,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>) &&>
          on_done,
      absl::Duration timeout = max_timeout) const override {
    DCHECK(encryption_enabled_);
    if (VLOG_IS_ON(6)) {
      VLOG(6) << "Raw request:\n" << raw_request->DebugString();
    }
    VLOG(5) << "Encrypting request ...";
    auto secret_request = EncryptRequestWithHpke<RawRequest, Request>(
        std::move(raw_request), *crypto_client_, *key_fetcher_manager_,
        cloud_platform_);
    if (!secret_request.ok()) {
      VLOG(1) << "Failed to encrypt the request: " << secret_request.status();
      auto error_status = absl::InternalError(kEncryptionFailed);
      return error_status;
    }
    auto& [hpke_secret, request] = *secret_request;
    VLOG(5) << "Encryption completed ...";

    auto params =
        std::make_unique<RawClientParams<Request, Response, RawResponse>>(
            std::move(request), std::move(on_done), metadata);
    params->SetDeadline(std::min(max_timeout, timeout));
    VLOG(5) << "Sending RPC ...";
    SendRpc(hpke_secret, params.release());
    return absl::OkStatus();
  }

 protected:
  using SecretRequest = std::pair<std::string, std::unique_ptr<Request>>;

  // Sends an asynchronous request via grpc. This method must be implemented
  // by classes implementing this interface.
  //
  // params: a pointer to the ClientParams object which carries data used
  // by the grpc stub.
  // hpke_secret: secret generated during HPKE encryption used during
  // AeadDecryption
  virtual void SendRpc(
      const std::string& hpke_secret,
      RawClientParams<Request, Response, RawResponse>* params) const {
    VLOG(5) << "Stub SendRpc invoked ...";
  }

  absl::StatusOr<std::unique_ptr<RawResponse>> DecryptResponse(
      const std::string& hpke_secret, Response* response) const {
    VLOG(6) << "Decrypting the response ...";
    absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
        decrypt_response = crypto_client_->AeadDecrypt(
            response->response_ciphertext(), hpke_secret);
    if (!decrypt_response.ok()) {
      const std::string error = absl::StrCat(
          "Could not decrypt response: ", decrypt_response.status().message());
      LOG(ERROR) << error;
      return absl::InternalError(error);
    }

    std::unique_ptr<RawResponse> raw_response = std::make_unique<RawResponse>();
    if (!raw_response->ParseFromString(decrypt_response->payload())) {
      const std::string error_msg =
          "Failed to parse proto from decrypted response";
      return absl::InvalidArgumentError(error_msg);
    }

    if (VLOG_IS_ON(6)) {
      VLOG(6) << "Decryption/decoding of response succeeded: "
              << raw_response->DebugString();
    }
    return raw_response;
  }

  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  CryptoClientWrapperInterface* crypto_client_;

  // Whether HPKE encryption is enabled for intra-server communication.
  bool encryption_enabled_;

  server_common::CloudPlatform cloud_platform_;
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
