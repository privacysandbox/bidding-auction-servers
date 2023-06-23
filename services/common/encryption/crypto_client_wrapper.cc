// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/encryption/crypto_client_wrapper.h"

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "cc/core/interface/errors.h"
#include "cc/public/cpio/interface/crypto_client/type_def.h"
#include "glog/logging.h"
#include "services/common/util/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::cmrt::sdk::crypto_service::v1::HpkeAead;
using ::google::cmrt::sdk::crypto_service::v1::HpkeEncryptedData;
using ::google::cmrt::sdk::crypto_service::v1::HpkeKdf;
using ::google::cmrt::sdk::crypto_service::v1::HpkeKem;
using ::google::cmrt::sdk::crypto_service::v1::HpkeParams;
using ::google::scp::core::ExecutionResult;
using ::google::scp::cpio::CryptoClientFactory;
using ::google::scp::cpio::CryptoClientOptions;

using ::google::scp::core::errors::GetErrorMessage;

using ::google::cmrt::sdk::crypto_service::v1::AeadDecryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptedData;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse;
using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse;
using ::google::cmrt::sdk::crypto_service::v1::HpkeEncryptRequest;
using ::google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse;
using ::google::cmrt::sdk::public_key_service::v1::PublicKey;

namespace {

absl::Status HandleCryptoOperationResult(const ExecutionResult& result,
                                         bool operation_successful,
                                         absl::string_view operation) {
  if (!result.Successful() || !operation_successful) {
    // Only log the execution result error; don't log the callback failure
    // twice.
    if (!result.Successful()) {
      const std::string error =
          absl::StrFormat(kCryptoOperationFailureError, operation.data(),
                          GetErrorMessage(result.status_code));
      LOG(ERROR) << error;
    }

    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrFormat("%s failed", operation.data()));
  }

  return absl::OkStatus();
}

}  //  namespace

CryptoClientWrapper::CryptoClientWrapper(
    std::unique_ptr<google::scp::cpio::CryptoClientInterface> crypto_client)
    : crypto_client_(std::move(crypto_client)) {
  crypto_client_->Init();
  crypto_client_->Run();
}

CryptoClientWrapper::~CryptoClientWrapper() { crypto_client_->Stop(); }

absl::StatusOr<HpkeEncryptResponse> CryptoClientWrapper::HpkeEncrypt(
    const PublicKey& key, absl::string_view plaintext_payload) noexcept {
  google::cmrt::sdk::public_key_service::v1::PublicKey public_key;
  public_key.set_key_id(key.key_id());
  public_key.set_public_key(key.public_key());

  HpkeEncryptRequest request;
  *request.mutable_public_key() = std::move(public_key);
  request.set_payload(plaintext_payload.data());
  request.set_shared_info(kSharedInfo);
  request.set_is_bidirectional(true);
  request.set_secret_length(
      google::cmrt::sdk::crypto_service::v1::SECRET_LENGTH_16_BYTES);

  HpkeEncryptResponse response;
  bool success = false;
  // HpkeEncrypt() callback is executed synchronously.
  ExecutionResult execution_result = crypto_client_->HpkeEncrypt(
      std::move(request),
      [&response, &success](
          const google::scp::core::ExecutionResult& result,
          const google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse&
              encrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(encrypt_response);
        } else {
          LOG(ERROR) << absl::StrFormat(kCryptoOperationFailureError,
                                        kHpkeEncrypt,
                                        GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(execution_result, success, kHpkeEncrypt));
  return response;
}

absl::StatusOr<AeadEncryptResponse> CryptoClientWrapper::AeadEncrypt(
    absl::string_view plaintext_payload, absl::string_view secret) noexcept {
  AeadEncryptRequest request;
  request.set_payload(plaintext_payload.data());
  request.set_secret(secret.data());
  request.set_shared_info(kSharedInfo);

  AeadEncryptResponse response;
  bool success = false;
  // AeadEncrypt() callback is executed synchronously.
  ExecutionResult execution_result = crypto_client_->AeadEncrypt(
      std::move(request),
      [&response, &success](const google::scp::core::ExecutionResult& result,
                            const AeadEncryptResponse& encrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(encrypt_response);
        } else {
          LOG(ERROR) << absl::StrFormat(kCryptoOperationFailureError,
                                        kAeadEncrypt,
                                        GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(execution_result, success, kAeadEncrypt));
  return response;
}

absl::StatusOr<HpkeDecryptResponse> CryptoClientWrapper::HpkeDecrypt(
    const server_common::PrivateKey& private_key,
    absl::string_view ciphertext) noexcept {
  // Only the private_key field needs to be set for decryption.
  google::cmrt::sdk::private_key_service::v1::PrivateKey key;
  key.set_private_key(private_key.private_key);

  // Only the ciphertext field needs to be set for decryption.
  HpkeEncryptedData encrypted_data;
  encrypted_data.set_ciphertext(ciphertext.data());

  HpkeDecryptRequest request;
  *request.mutable_private_key() = std::move(key);
  *request.mutable_encrypted_data() = std::move(encrypted_data);
  request.set_shared_info(kSharedInfo);
  request.set_is_bidirectional(true);
  request.set_secret_length(
      google::cmrt::sdk::crypto_service::v1::SECRET_LENGTH_16_BYTES);

  HpkeDecryptResponse response;
  bool success = false;
  // HpkeDecrypt() callback is executed synchronously.
  ExecutionResult execution_result = crypto_client_->HpkeDecrypt(
      std::move(request),
      [&response, &success](
          const google::scp::core::ExecutionResult& result,
          const google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse&
              decrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(decrypt_response);
        } else {
          LOG(ERROR) << absl::StrFormat(kCryptoOperationFailureError,
                                        kHpkeDecrypt,
                                        GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(execution_result, success, kHpkeDecrypt));
  return response;
}

absl::StatusOr<AeadDecryptResponse> CryptoClientWrapper::AeadDecrypt(
    absl::string_view ciphertext, absl::string_view secret) noexcept {
  AeadEncryptedData encrypted_data;
  encrypted_data.set_ciphertext(ciphertext.data());

  AeadDecryptRequest request;
  *request.mutable_encrypted_data() = std::move(encrypted_data);
  request.set_secret(secret.data());
  request.set_shared_info(kSharedInfo);

  AeadDecryptResponse response;
  bool success = false;
  // AeadDecrypt() callback is executed synchronously.
  ExecutionResult execution_result = crypto_client_->AeadDecrypt(
      std::move(request),
      [&response, &success](const google::scp::core::ExecutionResult& result,
                            const AeadDecryptResponse& decrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(decrypt_response);
        } else {
          LOG(ERROR) << absl::StrFormat(kCryptoOperationFailureError,
                                        kAeadDecrypt,
                                        GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(execution_result, success, kAeadDecrypt));
  return response;
}

}  // namespace privacy_sandbox::bidding_auction_servers
