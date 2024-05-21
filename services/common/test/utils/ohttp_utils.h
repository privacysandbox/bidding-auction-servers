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
inline constexpr char kTestPublicKey[] =
    "f3b7b2f1764f5c077effecad2afd86154596e63f7375ea522761b881e6c3c323";
inline constexpr char kTestPrivateKey[] =
    "e7b292f49df28b8065992cdeadbc9d032a0e09e8476cb6d8d507212e7be3b9b4";
inline constexpr uint8_t kTestKeyId = 64;

struct HpkeKeyset {
  // Defaults must match those found in FakeKeyFetcherManager.

  // Hex representation of the public key.
  std::string public_key = kTestPublicKey;
  // Hex representation of the private key.
  std::string private_key = kTestPrivateKey;
  // Key id.
  uint8_t key_id = kTestKeyId;
};

std::string GetHpkePrivateKey(absl::string_view private_key_hex);

std::string GetHpkePublicKey(absl::string_view public_key_hex);

// OHTTP Encrypt using passed or default public key.
// Note: `plaintext_payload` is consumed by this method and caller should not
// rely on it afterwards.
absl::StatusOr<quiche::ObliviousHttpRequest> CreateValidEncryptedRequest(
    std::string& plaintext_payload, const HpkeKeyset& keyset);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_UTILS_OHTTP_UTILS_H_
