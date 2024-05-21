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

#ifndef SERVICES_COMMON_ENCRYPTION_MOCK_CRYPTO_CLIENT_WRAPPER_H_
#define SERVICES_COMMON_ENCRYPTION_MOCK_CRYPTO_CLIENT_WRAPPER_H_

#include <string>

#include <include/gmock/gmock-matchers.h>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class MockCryptoClientWrapper : public CryptoClientWrapperInterface {
 public:
  virtual ~MockCryptoClientWrapper() = default;

  // NOLINTNEXTLINE
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>,
              HpkeEncrypt,
              (const google::cmrt::sdk::public_key_service::v1::PublicKey&,
               const std::string&),
              (noexcept, override));
  // NOLINTNEXTLINE
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>,
              HpkeDecrypt,
              (const server_common::PrivateKey& private_key,
               const std::string& ciphertext),
              (noexcept, override));
  // NOLINTNEXTLINE
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>,
              AeadEncrypt,
              (const std::string& plaintext_payload, const std::string& secret),
              (noexcept, override));
  // NOLINTNEXTLINE
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>,
              AeadDecrypt,
              (const std::string& plaintext_payload, const std::string& secret),
              (noexcept, override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ENCRYPTION_MOCK_CRYPTO_CLIENT_WRAPPER_H_
