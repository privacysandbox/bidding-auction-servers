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

#include "services/common/test/utils/ohttp_utils.h"

#include "absl/strings/escaping.h"
#include "services/common/util/oblivious_http_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

std::string GetHpkePrivateKey(absl::string_view private_key_hex) {
  return absl::HexStringToBytes(private_key_hex);
}

std::string GetHpkePublicKey(absl::string_view public_key_hex) {
  return absl::HexStringToBytes(public_key_hex);
}

absl::StatusOr<quiche::ObliviousHttpRequest> CreateValidEncryptedRequest(
    std::string& plaintext_payload, const HpkeKeyset& keyset) {
  return ToObliviousHTTPRequest(
      plaintext_payload, GetHpkePublicKey(keyset.public_key), keyset.key_id,
      EVP_HPKE_DHKEM_X25519_HKDF_SHA256, EVP_HPKE_HKDF_SHA256,
      EVP_HPKE_AES_256_GCM, kBiddingAuctionOhttpRequestLabel);
}

}  // namespace privacy_sandbox::bidding_auction_servers
