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

#ifndef SERVICES_COMMON_CODE_DISPATCH_CODE_DISPATCH_REACTOR_H_
#define SERVICES_COMMON_CODE_DISPATCH_CODE_DISPATCH_REACTOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/clients/code_dispatcher/code_dispatch_client.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
#include "src/cpp/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

// This is a gRPC reactor that serves a single Request.
// It stores state relevant to the request and after the
// response is finished being served, CodeDispatchReactor cleans up all
// necessary state and grpc releases the reactor from memory.
template <typename Request, typename RawRequest, typename Response,
          typename RawResponse>
class CodeDispatchReactor : public grpc::ServerUnaryReactor {
 public:
  explicit CodeDispatchReactor(
      CodeDispatchClient& dispatcher, const Request* request,
      Response* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client)
      : dispatcher_(dispatcher),
        request_(request),
        response_(response),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client) {
    PS_VLOG(5) << "Encryption is enabled, decrypting request now";
    if (DecryptRequest()) {
      PS_VLOG(3) << "Decrypted request: " << raw_request_.DebugString();
    } else {
      PS_VLOG(1) << "Failed to decrypt the request";
    }
  }

  // Polymorphic class => virtual destructor
  virtual ~CodeDispatchReactor() = default;

  // Initiate the asynchronous execution of the Request.
  // The function will call CodeDispatchClient and
  // will eventually modify the response_ member. When done, Execute will
  // call Finish(grpc::Status).
  virtual void Execute() = 0;

 protected:
  // Cleans up all state associated with the CodeDispatchReactor.
  // Called only after the grpc request is finalized and finished.
  void OnDone() override { delete this; };

  // Handles early-cancellation by the client.
  void OnCancel() override{
      // TODO(b/245982466): error handling design
  };

  // Decrypts the request ciphertext in and returns whether decryption was
  // successful. If successful, the result is written into 'raw_request_'.
  bool DecryptRequest() {
    if (request_->key_id().empty()) {
      PS_VLOG(1) << "No key ID found in the request";
      Finish(
          grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kEmptyKeyIdError));
      return false;
    } else if (request_->request_ciphertext().empty()) {
      PS_VLOG(1) << "No ciphertext found in the request";
      Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          kEmptyCiphertextError));
      return false;
    }

    std::optional<server_common::PrivateKey> private_key =
        key_fetcher_manager_->GetPrivateKey(request_->key_id());
    if (!private_key.has_value()) {
      PS_VLOG(1) << "Unable to fetch private key from the key fetcher manager";
      Finish(
          grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, kInvalidKeyIdError));
      return false;
    }

    absl::StatusOr<google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>
        decrypt_response = crypto_client_->HpkeDecrypt(
            *private_key, request_->request_ciphertext());
    if (!decrypt_response.ok()) {
      PS_VLOG(1) << "Unable to decrypt the request ciphertext: "
                 << decrypt_response.status();
      Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          kMalformedCiphertext));
      return false;
    }

    hpke_secret_ = std::move(decrypt_response->secret());
    return raw_request_.ParseFromString(decrypt_response->payload());
  }

  // Encrypts `raw_response` and sets the result on the 'response_ciphertext'
  // field in the response. Returns whether encryption was successful.
  bool EncryptResponse() {
    std::string payload = raw_response_.SerializeAsString();
    absl::StatusOr<google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
        aead_encrypt = crypto_client_->AeadEncrypt(payload, hpke_secret_);
    if (!aead_encrypt.ok()) {
      PS_VLOG(1) << "AEAD encrypt failed: " << aead_encrypt.status();
      Finish(grpc::Status(grpc::StatusCode::INTERNAL,
                          aead_encrypt.status().ToString()));
      return false;
    }

    response_->set_response_ciphertext(
        aead_encrypt->encrypted_data().ciphertext());
    return true;
  }

  // Dispatches execution requests to a library that runs V8 workers in
  // separate processes.
  CodeDispatchClient& dispatcher_;
  std::vector<DispatchRequest> dispatch_requests_;

  // The client request, lifecycle managed by gRPC.
  const Request* request_;
  RawRequest raw_request_;
  // The client response, lifecycle managed by gRPC.
  Response* response_;
  RawResponse raw_response_;

  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  CryptoClientWrapperInterface* crypto_client_;
  std::string hpke_secret_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CODE_DISPATCH_CODE_DISPATCH_REACTOR_H_
