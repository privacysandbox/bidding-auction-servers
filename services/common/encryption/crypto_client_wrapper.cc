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
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "proto/hpke.pb.h"
#include "proto/tink.pb.h"
#include "src/core/interface/errors.h"
#include "src/public/cpio/interface/crypto_client/type_def.h"
#include "src/util/status_macro/status_macros.h"

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

absl::Status HandleCryptoOperationResult(const absl::Status& status,
                                         bool operation_successful,
                                         const std::string& operation) {
  if (!status.ok() || !operation_successful) {
    // Only log the status; don't log the callback failure twice.
    if (!status.ok()) {
      ABSL_LOG(ERROR) << absl::StrFormat(kCryptoOperationFailureError,
                                         operation.data(), status.message());
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
  crypto_client_->Init().IgnoreError();
  crypto_client_->Run().IgnoreError();
}

CryptoClientWrapper::~CryptoClientWrapper() {
  crypto_client_->Stop().IgnoreError();
}

absl::StatusOr<HpkeEncryptResponse> CryptoClientWrapper::HpkeEncrypt(
    const PublicKey& key, const std::string& plaintext_payload) noexcept {
  google::cmrt::sdk::public_key_service::v1::PublicKey public_key;
  public_key.set_key_id(key.key_id());
  public_key.set_public_key(key.public_key());

  HpkeEncryptRequest request;
  *request.mutable_public_key() = std::move(public_key);
  request.set_payload(plaintext_payload);
  request.set_shared_info(kSharedInfo);
  request.set_is_bidirectional(true);
  request.set_secret_length(
      google::cmrt::sdk::crypto_service::v1::SECRET_LENGTH_32_BYTES);

  HpkeEncryptResponse response;
  bool success = false;
  // HpkeEncrypt() callback is executed synchronously.
  absl::Status status = crypto_client_->HpkeEncrypt(
      std::move(request),
      [&response, &success](
          const google::scp::core::ExecutionResult& result,
          google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
              encrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(encrypt_response);
        } else {
          ABSL_LOG(ERROR) << absl::StrFormat(
              kCryptoOperationFailureError, kHpkeEncrypt,
              GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(status, success, kHpkeEncrypt));
  return response;
}

absl::StatusOr<AeadEncryptResponse> CryptoClientWrapper::AeadEncrypt(
    const std::string& plaintext_payload, const std::string& secret) noexcept {
  AeadEncryptRequest request;
  request.set_payload(plaintext_payload);
  request.set_secret(secret);
  request.set_shared_info(kSharedInfo);

  AeadEncryptResponse response;
  bool success = false;
  // AeadEncrypt() callback is executed synchronously.
  absl::Status status = crypto_client_->AeadEncrypt(
      std::move(request),
      [&response, &success](const google::scp::core::ExecutionResult& result,
                            AeadEncryptResponse encrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(encrypt_response);
        } else {
          ABSL_LOG(ERROR) << absl::StrFormat(
              kCryptoOperationFailureError, kAeadEncrypt,
              GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(status, success, kAeadEncrypt));
  return response;
}

absl::StatusOr<HpkeDecryptResponse> CryptoClientWrapper::HpkeDecrypt(
    const server_common::PrivateKey& private_key,
    const std::string& ciphertext) noexcept {
  google::crypto::tink::HpkePrivateKey hpke_private_key;
  hpke_private_key.set_private_key(private_key.private_key);

  const int unused_key_id = 0;
  google::crypto::tink::Keyset keyset;
  keyset.set_primary_key_id(unused_key_id);
  keyset.add_key();
  keyset.mutable_key(0)->set_key_id(unused_key_id);
  keyset.mutable_key(0)->mutable_key_data()->set_value(
      hpke_private_key.SerializeAsString());

  // Only the private_key field needs to be set for decryption.
  google::cmrt::sdk::private_key_service::v1::PrivateKey key;
  key.set_key_id(private_key.key_id);
  key.set_private_key(absl::Base64Escape(keyset.SerializeAsString()));

  // Only the ciphertext field needs to be set for decryption.
  HpkeEncryptedData encrypted_data;
  encrypted_data.set_ciphertext(ciphertext);

  HpkeDecryptRequest request;
  *request.mutable_private_key() = std::move(key);
  *request.mutable_encrypted_data() = std::move(encrypted_data);
  request.set_shared_info(kSharedInfo);
  request.set_is_bidirectional(true);
  request.set_secret_length(
      google::cmrt::sdk::crypto_service::v1::SECRET_LENGTH_32_BYTES);

  HpkeDecryptResponse response;
  bool success = false;
  // HpkeDecrypt() callback is executed synchronously.
  absl::Status status = crypto_client_->HpkeDecrypt(
      std::move(request),
      [&response, &success](
          const google::scp::core::ExecutionResult& result,
          google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
              decrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(decrypt_response);
        } else {
          ABSL_LOG(ERROR) << absl::StrFormat(
              kCryptoOperationFailureError, kHpkeDecrypt,
              GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(status, success, kHpkeDecrypt));
  return response;
}

absl::StatusOr<AeadDecryptResponse> CryptoClientWrapper::AeadDecrypt(
    const std::string& ciphertext, const std::string& secret) noexcept {
  AeadDecryptRequest request;
  request.set_shared_info(kSharedInfo);
  request.set_secret(secret);
  request.mutable_encrypted_data()->set_ciphertext(ciphertext);

  AeadDecryptResponse response;
  bool success = false;
  // AeadDecrypt() callback is executed synchronously.
  absl::Status status = crypto_client_->AeadDecrypt(
      std::move(request),
      [&response, &success](const google::scp::core::ExecutionResult& result,
                            AeadDecryptResponse decrypt_response) {
        if (result.Successful()) {
          success = true;
          response = std::move(decrypt_response);
        } else {
          ABSL_LOG(ERROR) << absl::StrFormat(
              kCryptoOperationFailureError, kAeadDecrypt,
              GetErrorMessage(result.status_code));
        }
      });

  PS_RETURN_IF_ERROR(
      HandleCryptoOperationResult(status, success, kAeadDecrypt));
  return response;
}

}  // namespace privacy_sandbox::bidding_auction_servers
