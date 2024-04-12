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

#ifndef SERVICES_COMMON_UTIL_HPKE_UTILS_H_
#define SERVICES_COMMON_UTIL_HPKE_UTILS_H_

#include <string>

#include "absl/status/statusor.h"
#include "services/common/constants/user_error_strings.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

// This file defines methods that combine the usage and errors from
// key fetcher manager and crypto client to perform
// HPKE encryption and decryption.

// This struct represents an HPKE encrypted message
// and provides access to the related artifacts.
struct HpkeMessage {
  // String containing the encrypted object as a string.
  std::string ciphertext;

  // Key ID used to encrypt the message. Required for decryption.
  std::string key_id;

  // Secret used to encrypt the mesasage.
  std::string secret;
};

// Used to encrypt a payload with HPKE.
absl::StatusOr<HpkeMessage> HpkeEncrypt(
    // HPKE client requires const string reference.
    const std::string& plaintext, CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    const server_common::CloudPlatform& cloud_platform);

// Used to decrypt a payload with HPKE.
absl::StatusOr<std::string> HpkeDecrypt(
    // HPKE client requires const string reference.
    const std::string& ciphertext, absl::string_view key_id,
    CryptoClientWrapperInterface& crypto_client,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_HPKE_UTILS_H_
