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

#ifndef SERVICES_COMMON_TEST_UTILS_OHTTP_UTILS_H_
#define SERVICES_COMMON_TEST_UTILS_OHTTP_UTILS_H_

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "quiche/oblivious_http/oblivious_http_gateway.h"

namespace privacy_sandbox::bidding_auction_servers {

// Hardcoded test id for OHTTP key Config.
constexpr uint8_t kTestKeyId = 5;

// Default private key for testing.
constexpr absl::string_view kDefaultPrivateKey =
    "b77431ecfa8f4cfc30d6e467aafa06944dffe28cb9dd1409e33a3045f5adc8a1";

// Default public key for testing.
constexpr absl::string_view kDefaultPublicKey =
    "6d21cfe09fbea5122f9ebc2eb2a69fcc4f06408cd54aac934f012e76fcdcef62";

std::string GetHpkePrivateKey(
    absl::string_view private_key_hex = kDefaultPrivateKey);

std::string GetHpkePublicKey(
    absl::string_view public_key_hex = kDefaultPublicKey);

// OHTTP Encrypt using passed or default public key.
absl::StatusOr<quiche::ObliviousHttpRequest> CreateValidEncryptedRequest(
    const std::string& plaintext_payload,
    absl::string_view public_key_hex = kDefaultPublicKey);

// HPKE Encrypt response.
absl::StatusOr<std::string> EncryptAndEncapsulateResponse(
    std::string plaintext_data, quiche::ObliviousHttpRequest::Context& context,
    absl::string_view private_key_hex);

// OHTTP Decrypt using passed or default public key.
absl::StatusOr<quiche::ObliviousHttpResponse> DecryptEncapsulatedResponse(
    absl::string_view encapsulated_response,
    quiche::ObliviousHttpRequest::Context& oblivious_request_context,
    absl::string_view public_key_hex = kDefaultPublicKey);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_UTILS_OHTTP_UTILS_H_
