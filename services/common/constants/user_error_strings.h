/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_CONSTANTS_USER_ERROR_STRINGS_H_
#define SERVICES_COMMON_CONSTANTS_USER_ERROR_STRINGS_H_

// Constants for user facing error messages shared across B&A servers.
namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kEmptyKeyIdError[] = "key_id must be non-null.";
inline constexpr char kEmptyCiphertextError[] =
    "request_ciphertext must be non-null.";
inline constexpr char kInvalidKeyIdError[] = "Invalid key_id.";
inline constexpr char kMalformedRequest[] = "Malformed request.";
inline constexpr char kMalformedCiphertext[] = "Malformed request ciphertext.";
inline constexpr char kMissingInputs[] = "Missing inputs";

inline constexpr char kInternalServerError[] = "Internal server error.";
inline constexpr char kSellerDomainEmpty[] =
    "Seller domain in server must be configured.";

inline constexpr char kUnsupportedMetadataValues[] =
    "Unsupported version and compression combination";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CONSTANTS_USER_ERROR_STRINGS_H_
