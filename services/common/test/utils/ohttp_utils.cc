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

namespace privacy_sandbox::bidding_auction_servers {

namespace {

const quiche::ObliviousHttpHeaderKeyConfig GetOhttpKeyConfig(uint8_t key_id,
                                                             uint16_t kem_id,
                                                             uint16_t kdf_id,
                                                             uint16_t aead_id) {
  const auto ohttp_key_config = quiche::ObliviousHttpHeaderKeyConfig::Create(
      key_id, kem_id, kdf_id, aead_id);
  return std::move(ohttp_key_config.value());
}

}  // namespace

std::string GetHpkePrivateKey(absl::string_view private_key_hex) {
  return absl::HexStringToBytes(private_key_hex);
}

std::string GetHpkePublicKey(absl::string_view public_key_hex) {
  return absl::HexStringToBytes(public_key_hex);
}

absl::StatusOr<quiche::ObliviousHttpRequest> CreateValidEncryptedRequest(
    const std::string& plaintext_payload, const HpkeKeyset& keyset) {
  const auto config =
      GetOhttpKeyConfig(keyset.key_id, EVP_HPKE_DHKEM_X25519_HKDF_SHA256,
                        EVP_HPKE_HKDF_SHA256, EVP_HPKE_AES_256_GCM);
  return quiche::ObliviousHttpRequest::CreateClientObliviousRequest(
      std::move(plaintext_payload), GetHpkePublicKey(keyset.public_key), config,
      kBiddingAuctionOhttpRequestLabel);
}

absl::StatusOr<quiche::ObliviousHttpResponse> DecryptEncapsulatedResponse(
    absl::string_view encapsulated_response,
    quiche::ObliviousHttpRequest::Context& oblivious_request_context,
    const HpkeKeyset& keyset) {
  const auto config =
      GetOhttpKeyConfig(keyset.key_id, EVP_HPKE_DHKEM_X25519_HKDF_SHA256,
                        EVP_HPKE_HKDF_SHA256, EVP_HPKE_AES_256_GCM);
  const auto http_client = quiche::ObliviousHttpClient::Create(
      GetHpkePublicKey(keyset.public_key), config);
  return quiche::ObliviousHttpResponse::CreateClientObliviousResponse(
      absl::StrCat(encapsulated_response), oblivious_request_context,
      kBiddingAuctionOhttpResponseLabel);
}

}  // namespace privacy_sandbox::bidding_auction_servers
