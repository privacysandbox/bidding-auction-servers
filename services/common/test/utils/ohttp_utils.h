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

// Custom media types for B&A. Used as input to request decryption/response
// encryption.
inline constexpr absl::string_view kBiddingAuctionOhttpRequestLabel =
    "message/auction request";
inline constexpr absl::string_view kBiddingAuctionOhttpResponseLabel =
    "message/auction response";

// Hardcoded test id for OHTTP key Config.
constexpr uint8_t kTestKeyId = 64;

// Default private key for testing.
constexpr absl::string_view kDefaultPrivateKey =
    "e7b292f49df28b8065992cdeadbc9d032a0e09e8476cb6d8d507212e7be3b9b4";

// Default public key for testing.
constexpr absl::string_view kDefaultPublicKey =
    "f3b7b2f1764f5c077effecad2afd86154596e63f7375ea522761b881e6c3c323";

std::string GetHpkePrivateKey(
    absl::string_view private_key_hex = kDefaultPrivateKey);

std::string GetHpkePublicKey(
    absl::string_view public_key_hex = kDefaultPublicKey);

// OHTTP Encrypt using passed or default public key.
absl::StatusOr<quiche::ObliviousHttpRequest> CreateValidEncryptedRequest(
    const std::string& plaintext_payload,
    absl::string_view public_key_hex = kDefaultPublicKey,
    uint8_t key_id = kTestKeyId);

// OHTTP Decrypt using passed or default public key.
absl::StatusOr<quiche::ObliviousHttpResponse> DecryptEncapsulatedResponse(
    absl::string_view encapsulated_response,
    quiche::ObliviousHttpRequest::Context& oblivious_request_context,
    absl::string_view public_key_hex = kDefaultPublicKey,
    uint8_t key_id = kTestKeyId);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_UTILS_OHTTP_UTILS_H_
