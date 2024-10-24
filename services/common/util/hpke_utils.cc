/*
 * Copyright 2024 Google LLC
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

#include "services/common/util/hpke_utils.h"

#include <utility>

namespace privacy_sandbox::bidding_auction_servers {
absl::StatusOr<HpkeMessage> HpkeEncrypt(
    const std::string& plaintext, CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    const server_common::CloudPlatform& cloud_platform) {
  auto key = key_fetcher_manager.GetPublicKey(cloud_platform);
  if (!key.ok()) {
    std::string error =
        absl::StrCat("Could not find public key for HPKE encryption: ",
                     key.status().message());
    ABSL_LOG(ERROR) << error;
    return absl::InternalError(std::move(error));
  }

  auto encrypt_response = crypto_client.HpkeEncrypt(key.value(), plaintext);

  if (!encrypt_response.ok()) {
    std::string error = absl::StrCat("Failed encrypting request: ",
                                     encrypt_response.status().message());
    ABSL_LOG(ERROR) << error;
    return absl::InternalError(std::move(error));
  }
  HpkeMessage output;
  output.ciphertext = std::move(
      *encrypt_response->mutable_encrypted_data()->mutable_ciphertext());
  output.key_id = std::move(*key->mutable_key_id());
  output.secret = std::move(*encrypt_response->mutable_secret());
  return output;
}

absl::StatusOr<std::string> HpkeDecrypt(
    // HPKE client requires const string reference.
    const std::string& ciphertext, absl::string_view key_id,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager) {
  if (key_id.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, kInvalidKeyIdError);
  }
  if (ciphertext.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        kMalformedCiphertext);
  }
  std::optional<server_common::PrivateKey> private_key =
      key_fetcher_manager.GetPrivateKey(key_id.data());
  if (!private_key) {
    return absl::Status(absl::StatusCode::kInvalidArgument, kInvalidKeyIdError);
  }
  auto decrypt_response = crypto_client.HpkeDecrypt(*private_key, ciphertext);
  if (!decrypt_response.ok()) {
    std::string error = absl::StrCat("Failed decrypting request: ",
                                     decrypt_response.status().message());
    ABSL_LOG(ERROR) << error;
    return absl::Status(absl::StatusCode::kInvalidArgument, std::move(error));
  }
  return std::move(*std::move(decrypt_response)->mutable_payload());
}

}  // namespace privacy_sandbox::bidding_auction_servers
