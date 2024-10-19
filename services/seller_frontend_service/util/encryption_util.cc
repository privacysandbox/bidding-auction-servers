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

#include "services/seller_frontend_service/util/encryption_util.h"

#include <utility>

#include "absl/strings/escaping.h"
#include "services/common/util/oblivious_http_utils.h"
#include "services/common/util/request_response_constants.h"
#include "src/include/openssl/hpke.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>>
DecryptOHTTPEncapsulatedHpkeCiphertext(
    absl::string_view ciphertext,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager) {
  absl::StatusOr<server_common::EncapsulatedRequest>
      parsed_encapsulated_request =
          server_common::ParseEncapsulatedRequest(ciphertext);
  if (!parsed_encapsulated_request.ok()) {
    PS_VLOG(kNoisyWarn, SystemLogContext())
        << "Error while parsing encapsulated ciphertext: "
        << parsed_encapsulated_request.status();
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Error while parsing encapsulated ciphertext: ",
                     parsed_encapsulated_request.status()));
  }

  // Parse the encapsulated request for the key ID.
  absl::StatusOr<uint8_t> key_id = quiche::ObliviousHttpHeaderKeyConfig::
      ParseKeyIdFromObliviousHttpRequestPayload(
          parsed_encapsulated_request->request_payload);
  if (!key_id.ok()) {
    PS_VLOG(kNoisyWarn, SystemLogContext())
        << "Parsed key id error status: " << key_id.status().message();
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        kInvalidOhttpKeyIdError);
  }
  PS_VLOG(5) << "Key Id parsed correctly";

  std::string str_key_id = std::to_string(*key_id);
  std::optional<server_common::PrivateKey> private_key =
      key_fetcher_manager.GetPrivateKey(str_key_id);

  if (!private_key) {
    PS_VLOG(kNoisyWarn, SystemLogContext())
        << "Unable to retrieve private key for key ID: " << str_key_id;
    return absl::Status(absl::StatusCode::kNotFound,
                        absl::StrFormat(kMissingPrivateKey, str_key_id));
  }

  PS_VLOG(kSuccess) << "Private Key Id: " << private_key->key_id
                    << ", Key Hex: "
                    << absl::BytesToHexString(private_key->private_key);
  // Decrypt the ciphertext.
  absl::StatusOr<quiche::ObliviousHttpRequest> ohttp_request =
      server_common::DecryptEncapsulatedRequest(*private_key,
                                                *parsed_encapsulated_request);
  if (!ohttp_request.ok()) {
    PS_VLOG(kNoisyWarn, SystemLogContext())
        << "Unable to decrypt the ciphertext. Reason: "
        << ohttp_request.status().message();
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrFormat(kMalformedEncapsulatedRequest,
                                        ohttp_request.status().message()));
  }

  PS_VLOG(5) << "Successfully decrypted input ciphertext.";

  return std::make_unique<OhttpHpkeDecryptedMessage>(
      *ohttp_request, *private_key, parsed_encapsulated_request->request_label);
}

absl::StatusOr<OhttpHpkeEncryptedMessage> HpkeEncryptAndOHTTPEncapsulate(
    std::string plaintext, absl::string_view request_label,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager,
    const server_common::CloudPlatform& cloud_platform) {
  if (request_label != server_common::kBiddingAuctionOhttpRequestLabel &&
      request_label !=
          quiche::ObliviousHttpHeaderKeyConfig::kOhttpRequestLabel) {
    return absl::Status(
        absl::StatusCode::kInvalidArgument,
        absl::StrCat("Request label must be one of: ",
                     server_common::kBiddingAuctionOhttpRequestLabel, ", ",
                     quiche::ObliviousHttpHeaderKeyConfig::kOhttpRequestLabel));
  }
  auto public_key = key_fetcher_manager.GetPublicKey(cloud_platform);
  if (!public_key.ok()) {
    std::string error =
        absl::StrCat("Could not find public key for encryption: ",
                     public_key.status().message());
    ABSL_LOG(ERROR) << error;
    return absl::InternalError(std::move(error));
  }

  std::string unescaped_public_key_bytes;
  if (!absl::Base64Unescape(public_key->public_key(),
                            &unescaped_public_key_bytes)) {
    return absl::InternalError(
        absl::StrCat("Failed to base64 decode the fetched public key: ",
                     public_key->public_key()));
  }

  auto request = ToObliviousHTTPRequest(
      plaintext, unescaped_public_key_bytes, std::stoi(public_key->key_id()),
      // Must match the ones used by server_common::DecryptEncapsulatedRequest.
      EVP_HPKE_DHKEM_X25519_HKDF_SHA256, EVP_HPKE_HKDF_SHA256,
      EVP_HPKE_AES_256_GCM, request_label);
  if (!request.ok()) {
    return request.status();
  }
  return OhttpHpkeEncryptedMessage(*std::move(request), request_label);
}

OhttpHpkeEncryptedMessage::OhttpHpkeEncryptedMessage(
    quiche::ObliviousHttpRequest ohttp_request, absl::string_view request_label)
    // Prepend with a zero byte to follow the new B&A request format that uses
    // custom media types for request encryption/request decryption.
    : ciphertext(request_label ==
                         server_common::kBiddingAuctionOhttpRequestLabel
                     ? '\0' + ohttp_request.EncapsulateAndSerialize()
                     : ohttp_request.EncapsulateAndSerialize()),
      context(std::move(ohttp_request).ReleaseContext()) {}

OhttpHpkeDecryptedMessage::OhttpHpkeDecryptedMessage(
    quiche::ObliviousHttpRequest& decrypted_request,
    server_common::PrivateKey& private_key, absl::string_view request_label)
    : private_key(std::move(private_key)),
      request_label(request_label),
      plaintext(decrypted_request.GetPlaintextData()),
      context(std::move(decrypted_request).ReleaseContext()) {}

OhttpHpkeDecryptedMessage::OhttpHpkeDecryptedMessage(
    std::string plaintext, quiche::ObliviousHttpRequest::Context& context,
    server_common::PrivateKey& private_key, absl::string_view request_label)
    : private_key(std::move(private_key)),
      request_label(request_label),
      plaintext(std::move(plaintext)),
      context(std::move(context)) {}

}  // namespace privacy_sandbox::bidding_auction_servers
