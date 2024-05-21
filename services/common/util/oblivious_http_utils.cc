// Copyright 2024 Google LLC
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

#include "services/common/util/oblivious_http_utils.h"

#include "absl/strings/escaping.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "src/include/openssl/hpke.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<quiche::ObliviousHttpRequest> ToObliviousHTTPRequest(
    std::string& binary_http_message, absl::string_view public_key_bytes,
    uint8_t key_id, uint16_t kem_id, uint16_t kdf_id, uint16_t aead_id,
    absl::string_view request_label) {
  auto ohttp_key_config = quiche::ObliviousHttpHeaderKeyConfig::Create(
      key_id, kem_id, kdf_id, aead_id);

  PS_VLOG(5) << "Creating Oblivious HTTP request using public key "
             << "id = " << int(key_id);
  return quiche::ObliviousHttpRequest::CreateClientObliviousRequest(
      std::move(binary_http_message), public_key_bytes,
      *std::move(ohttp_key_config), request_label);
}

absl::StatusOr<std::string> FromObliviousHTTPResponse(
    std::string& oblivious_http_response,
    quiche::ObliviousHttpRequest::Context& context,
    absl::string_view response_label) {
  PS_ASSIGN_OR_RETURN(
      auto decrypted_response,
      quiche::ObliviousHttpResponse::CreateClientObliviousResponse(
          std::move(oblivious_http_response), context, response_label));
  PS_VLOG(7) << "Plaintext binary http response received: "
             << decrypted_response.GetPlaintextData();
  return decrypted_response.GetPlaintextData();
}

}  // namespace privacy_sandbox::bidding_auction_servers
