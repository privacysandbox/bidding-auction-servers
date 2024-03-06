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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_ENCRYPTION_UTIL_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_ENCRYPTION_UTIL_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"
#include "src/cpp/communication/ohttp_utils.h"
#include "src/cpp/encryption/key_fetcher/src/key_fetcher_manager.h"
#include "src/cpp/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

// Constants for errors.
inline constexpr char kInvalidOhttpKeyIdError[] =
    "Invalid key ID provided in OHTTP encapsulated request";
inline constexpr char kMissingPrivateKey[] =
    "Unable to get private key for the key ID in OHTTP encapsulated request. "
    "Key ID: %s";
inline constexpr char kMalformedEncapsulatedRequest[] =
    "Malformed OHTTP encapsulated request provided %s";

// This struct holds elements related to an HPKE
// decrypted message which was encrypted using the OHTTP
// framing standard. The related artifacts can be used
// to encrypt responses meant for the clients who sent
// the encrypted message.
struct OhttpHpkeDecryptedMessage {
  // Private key corresponding to the public key used to encrypt the request.
  server_common::PrivateKey private_key;

  // Media type used to encrypt the request.
  // Refers to inline constexpr char[] labels.
  absl::string_view request_label;

  // Decrypted plaintext.
  std::string plaintext;

  // OHTTP context required for decryption.
  quiche::ObliviousHttpRequest::Context context;

  explicit OhttpHpkeDecryptedMessage(
      quiche::ObliviousHttpRequest& decrypted_request,
      server_common::PrivateKey& private_key, absl::string_view request_label);
};

// Decrypt a payload encrypted with OHTTP based HPKE using the common library
// DecryptEncapsulatedRequest method. Returns heap object for easy assignment
// since quiche::ObliviousHttpRequest::Context doesn't have a default
// constructor.
absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>>
DecryptOHTTPEncapsulatedHpkeCiphertext(
    absl::string_view ciphertext,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_ENCRYPTION_UTIL_H_
