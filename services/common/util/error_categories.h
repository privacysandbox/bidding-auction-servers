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

#ifndef SERVICES_COMMON_UTIL_ERROR_CATEGORIES_H_
#define SERVICES_COMMON_UTIL_ERROR_CATEGORIES_H_

namespace privacy_sandbox::bidding_auction_servers {

// We have at least two types of errors that can be reported by the services:
//
//  1) Errors that need to be reported back to Seller Ad server (e.g.
//     incorrect buyer addresses).
//
//  2) Errors that need to be reported back to the clients (e.g. bad buyer
//     inputs or issues related to encoding/decoding).
enum class ErrorVisibility { CLIENT_VISIBLE, AD_SERVER_VISIBLE };

// Later we can leverage more fine grained status codes here.
enum class ErrorCode { CLIENT_SIDE = 400, SERVER_SIDE = 500 };

// Client side errors listed here.
inline constexpr char kMissingGenerationId[] =
    "Request is missing generation ID";
inline constexpr char kMissingPublisherName[] =
    "Request is missing publisher name";
inline constexpr char kMissingBuyerInputs[] = "Request is missing buyer inputs";
inline constexpr char kMissingInterestGroupsAndProtectedSignals[] =
    "Request is missing interest groups and protected signals for buyer: %s";
inline constexpr char kEmptyInterestGroupOwner[] =
    "One or more interest group owner name is empty in buyer inputs";
inline constexpr char kNonEmptyBuyerInputMalformed[] =
    "BuyerInput map is present but malformed: %s";
inline constexpr char kBadProtectedAudienceBinaryProto[] =
    "Unable to decode ProtectedAudienceInput binary proto";
inline constexpr char kBadCompressedBuyerInput[] =
    "Unable to decompress buyer input for buyer: %s";
inline constexpr char kBadBuyerInputProto[] =
    "Unable to decode BuyerInput binary proto for buyer: %s";
inline constexpr char kMalformedBuyerInput[] = "Malformed buyer input.";
inline constexpr char kErrorDecryptingCiphertextError[] =
    "Error while decrypting protected_auction_ciphertext: %s.";

// Server side errors listed here.
inline constexpr char kInternalError[] = "Internal Error";
inline constexpr char kEncryptionFailed[] = "Encryption Failure.";
inline constexpr char kRequestCancelled[] = "Request Cancelled by Client.";

// Error handling related constants.
inline constexpr char kErrorDelimiter[] = "; ";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_ERROR_CATEGORIES_H_
