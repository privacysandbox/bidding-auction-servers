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

#include <include/gmock/gmock-matchers.h>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "include/gtest/gtest.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class MockCryptoClientWrapper : public CryptoClientWrapperInterface {
 public:
  virtual ~MockCryptoClientWrapper() = default;

  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse>,
              HpkeEncrypt,
              (const google::cmrt::sdk::public_key_service::v1::PublicKey&,
               absl::string_view),
              (noexcept, override));
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse>,
              HpkeDecrypt,
              (const server_common::PrivateKey& private_key,
               absl::string_view ciphertext),
              (noexcept, override));
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse>,
              AeadEncrypt,
              (absl::string_view plaintext_payload, absl::string_view secret),
              (noexcept, override));
  MOCK_METHOD(absl::StatusOr<
                  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse>,
              AeadDecrypt,
              (absl::string_view plaintext_payload, absl::string_view secret),
              (noexcept, override));
};

}  // namespace privacy_sandbox::bidding_auction_servers
