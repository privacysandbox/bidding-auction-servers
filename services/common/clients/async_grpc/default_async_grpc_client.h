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

#include "absl/log/check.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/async_grpc/grpc_client_utils.h"
#include "services/common/clients/async_grpc/request_config.h"
#include "services/common/clients/client_params.h"
#include "services/common/constants/common_constants.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/client_context_util.h"
#include "services/common/util/error_categories.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

// This can be made configurable
inline constexpr absl::Duration kMaxClientTimeout = absl::Seconds(60);

// This class acts as a template for a basic asynchronous grpc client.
template <typename Request, typename Response, typename RawRequest,
          typename RawResponse>
class DefaultAsyncGrpcClient
    : public AsyncClient<Request, Response, RawRequest, RawResponse> {
 public:
  DefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client)
      : AsyncClient<Request, Response, RawRequest, RawResponse>(),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client) {
#if defined(CLOUD_PLATFORM_AWS)
    cloud_platform_ = server_common::CloudPlatform::kAws;
#elif defined(CLOUD_PLATFORM_GCP)
    cloud_platform_ = server_common::CloudPlatform::kGcp;
#else
    cloud_platform_ = server_common::CloudPlatform::kLocal;
#endif
  }

  DefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      server_common::CloudPlatform cloud_platform)
      : AsyncClient<Request, Response, RawRequest, RawResponse>(),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client),
        cloud_platform_(cloud_platform) {}

  DefaultAsyncGrpcClient(const DefaultAsyncGrpcClient&) = delete;
  DefaultAsyncGrpcClient& operator=(const DefaultAsyncGrpcClient&) = delete;

  absl::Status ExecuteInternal(
      std::unique_ptr<RawRequest> raw_request, grpc::ClientContext* context,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>,
                              ResponseMetadata) &&>
          on_done,
      absl::Duration timeout = kMaxClientTimeout,
      RequestConfig request_config = {}) override {
    PS_VLOG(6) << "Raw request:\n" << raw_request->DebugString();
    PS_VLOG(5) << "Encrypting request ...";

    return EncryptPayloadAndSendRpc(raw_request->SerializeAsString(), context,
                                    std::move(on_done), timeout,
                                    request_config);
  }

 protected:
  using SecretRequest = std::pair<std::string, std::unique_ptr<Request>>;

  absl::Status EncryptPayloadAndSendRpc(
      const std::string& plaintext, grpc::ClientContext* context,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>,
                              ResponseMetadata) &&>
          on_done,
      absl::Duration timeout, RequestConfig request_config = {}) {
    auto secret_request = EncryptRequestWithHpke<Request>(
        plaintext, *crypto_client_, *key_fetcher_manager_, cloud_platform_);
    if (!secret_request.ok()) {
      PS_LOG(ERROR, SystemLogContext())
          << "Failed to encrypt the request: " << secret_request.status();
      return absl::InternalError(kEncryptionFailed);
    }
    auto& [hpke_secret, request] = *secret_request;
    PS_VLOG(5) << "Encryption completed ...";

    auto params =
        std::make_unique<RawClientParams<Request, Response, RawResponse>>(
            std::move(request), std::move(on_done), request_config);
    context->set_deadline(GetClientContextDeadline(timeout, kMaxClientTimeout));
    PS_VLOG(5) << "Sending RPC ...";
    SendRpc(hpke_secret, context, params.release());
    return absl::OkStatus();
  }

  // Sends an asynchronous request via grpc. This method must be implemented
  // by classes implementing this interface.
  //
  // params: a pointer to the ClientParams object which carries data used
  // by the grpc stub.
  // hpke_secret: secret generated during HPKE encryption used during
  // AeadDecryption
  virtual void SendRpc(
      const std::string& hpke_secret, grpc::ClientContext* context,
      RawClientParams<Request, Response, RawResponse>* params) const {
    PS_VLOG(5) << "Stub SendRpc invoked ...";
  }

  absl::StatusOr<std::unique_ptr<RawResponse>> DecryptResponse(
      const std::string& hpke_secret, Response* response) const {
    PS_VLOG(6) << "Decrypting the response ...";
    absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
        decrypt_response = crypto_client_->AeadDecrypt(
            response->response_ciphertext(), hpke_secret);
    if (!decrypt_response.ok()) {
      const std::string error = absl::StrCat(
          "Could not decrypt response: ", decrypt_response.status().message());
      ABSL_LOG(ERROR) << error;
      return absl::InternalError(error);
    }

    std::unique_ptr<RawResponse> raw_response = std::make_unique<RawResponse>();
    if (!raw_response->ParseFromString(decrypt_response->payload())) {
      const std::string error_msg =
          "Failed to parse proto from decrypted response";
      return absl::InvalidArgumentError(error_msg);
    }

    PS_VLOG(6) << "Decryption/decoding of response succeeded: "
               << raw_response->DebugString();
    return raw_response;
  }

  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  CryptoClientWrapperInterface* crypto_client_;
  std::string ca_cert_;

  server_common::CloudPlatform cloud_platform_;
};

// Creates a shared grpc channel from a given server URL. This channel
// is passed into stubs owned by specific implementations of gRPC clients
// since the stub type is based on the service.
// server_addr: the URL or IP for the server DNS
// compression: flag to enable gRPC level compression for the client.
// Disabled by default.
std::shared_ptr<grpc::Channel> CreateChannel(
    // Const string reference to prevent copies. string_view cannot be casted
    // to argument for grpc::CreateChannel.
    absl::string_view server_addr, bool compression = false, bool secure = true,
    absl::string_view grpc_arg_default_authority = "",
    absl::string_view ca_cert = "/etc/roots.pem");

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_ASYNC_GRPC_CLIENT_H_
