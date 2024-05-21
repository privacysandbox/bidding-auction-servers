/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_ENCRYPTION_CRYPTO_CLIENT_WRAPPER_INTERFACE_H_
#define SERVICES_COMMON_ENCRYPTION_CRYPTO_CLIENT_WRAPPER_INTERFACE_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/encryption/key_fetcher/interface/private_key_fetcher_interface.h"
#include "src/public/cpio/interface/crypto_client/crypto_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class CryptoClientWrapperInterface {
 public:
  // Waits for any in-flight key fetch flows to complete and terminates any
  // resources used by the manager.
  virtual ~CryptoClientWrapperInterface() = default;

  // Decrypts a ciphertext using HPKE.
  virtual absl::StatusOr<
      google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>
  HpkeDecrypt(const server_common::PrivateKey& private_key,
              const std::string& ciphertext) noexcept = 0;

  // Encrypts a plaintext payload using HPKE and the provided public key.
  virtual absl::StatusOr<
      google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>
  HpkeEncrypt(const google::cmrt::sdk::public_key_service::v1::PublicKey& key,
              const std::string& plaintext_payload) noexcept = 0;

  // Encrypts plaintext payload using AEAD and a secret derived from the HPKE
  // decrypt operation.
  virtual absl::StatusOr<
      google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>
  AeadEncrypt(const std::string& plaintext_payload,
              const std::string& secret) noexcept = 0;

  // Decrypts a ciphertext using AEAD and a secret derived from the HPKE
  // encrypt operation.
  virtual absl::StatusOr<
      google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>
  AeadDecrypt(const std::string& ciphertext,
              const std::string& secret) noexcept = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ENCRYPTION_CRYPTO_CLIENT_WRAPPER_INTERFACE_H_
