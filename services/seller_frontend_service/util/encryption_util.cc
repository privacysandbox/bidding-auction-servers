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

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<std::unique_ptr<OhttpHpkeDecryptedMessage>>
DecryptOHTTPEncapsulatedHpkeCiphertext(
    absl::string_view ciphertext,
    server_common::KeyFetcherManagerInterface& key_fetcher_manager) {
  absl::StatusOr<server_common::EncapsulatedRequest>
      parsed_encapsulated_request =
          server_common::ParseEncapsulatedRequest(ciphertext);
  if (!parsed_encapsulated_request.ok()) {
    PS_VLOG(2) << "Error while parsing encapsulated ciphertext: "
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
    PS_VLOG(2) << "Parsed key id error status: " << key_id.status().message();
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        kInvalidOhttpKeyIdError);
  }
  PS_VLOG(5) << "Key Id parsed correctly";

  std::string str_key_id = std::to_string(*key_id);
  std::optional<server_common::PrivateKey> private_key =
      key_fetcher_manager.GetPrivateKey(str_key_id);

  if (!private_key.has_value()) {
    PS_VLOG(2) << "Unable to retrieve private key for key ID: " << str_key_id;
    return absl::Status(
        absl::StatusCode::kNotFound,
        absl::StrFormat(kMissingPrivateKey, std::move(str_key_id)));
  }

  PS_VLOG(3) << "Private Key Id: " << private_key->key_id << ", Key Hex: "
             << absl::BytesToHexString(private_key->private_key);
  // Decrypt the ciphertext.
  absl::StatusOr<quiche::ObliviousHttpRequest> ohttp_request =
      server_common::DecryptEncapsulatedRequest(*private_key,
                                                *parsed_encapsulated_request);
  if (!ohttp_request.ok()) {
    PS_VLOG(2) << "Unable to decrypt the ciphertext. Reason: "
               << ohttp_request.status().message();
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrFormat(kMalformedEncapsulatedRequest,
                                        ohttp_request.status().message()));
  }

  PS_VLOG(5) << "Successfully decrypted input ciphertext.";

  return std::make_unique<OhttpHpkeDecryptedMessage>(
      *ohttp_request, *private_key, parsed_encapsulated_request->request_label);
}

OhttpHpkeDecryptedMessage::OhttpHpkeDecryptedMessage(
    quiche::ObliviousHttpRequest& decrypted_request,
    server_common::PrivateKey& private_key, absl::string_view request_label)
    : private_key(std::move(private_key)),
      request_label(request_label),
      plaintext(decrypted_request.GetPlaintextData()),
      context(std::move(decrypted_request).ReleaseContext()) {}

}  // namespace privacy_sandbox::bidding_auction_servers
