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

#ifndef SERVICES_COMMON_UTIL_OBLIVIOUS_HTTP_UTILS_H_
#define SERVICES_COMMON_UTIL_OBLIVIOUS_HTTP_UTILS_H_

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "src/include/openssl/hpke.h"

namespace privacy_sandbox::bidding_auction_servers {

// Encapsulates the provided binary HTTP message in an Oblivious HTTP request
// using the public key and other HPKE params provided.
// Note that `binary_http_message` is consumed by this method and hence should
// not be used by the caller afterwards.
absl::StatusOr<quiche::ObliviousHttpRequest> ToObliviousHTTPRequest(
    std::string& binary_http_message, absl::string_view public_key_bytes,
    uint8_t key_id, uint16_t kem_id = EVP_HPKE_DHKEM_X25519_HKDF_SHA256,
    uint16_t kdf_id = EVP_HPKE_HKDF_SHA256,
    uint16_t aead_id = EVP_HPKE_AES_256_GCM,
    absl::string_view request_label =
        quiche::ObliviousHttpHeaderKeyConfig::kOhttpRequestLabel);

// Decapsulates the binary HTTP message from the provided Oblivious HTTP
// response using the provided encryption context.
// Note that `oblivious_http_response` is consumed by this method and hence
// should not be used by the caller afterwards.
absl::StatusOr<std::string> FromObliviousHTTPResponse(
    std::string& oblivious_http_response,
    quiche::ObliviousHttpRequest::Context& context,
    absl::string_view response_label =
        quiche::ObliviousHttpHeaderKeyConfig::kOhttpResponseLabel);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_OBLIVIOUS_HTTP_UTILS_H_
