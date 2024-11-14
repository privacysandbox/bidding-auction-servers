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

#include <cstdint>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// We have at least two types of errors that can be reported by the services:
//
//  1) Errors that need to be reported back to Seller Ad server (e.g.
//     incorrect buyer addresses).
//
//  2) Errors that need to be reported back to the clients (e.g. bad buyer
//     inputs or issues related to encoding/decoding).
enum class ErrorVisibility : std::uint8_t { CLIENT_VISIBLE, AD_SERVER_VISIBLE };

// Later we can leverage more fine grained status codes here.
enum class ErrorCode : std::uint16_t { CLIENT_SIDE = 400, SERVER_SIDE = 500 };

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
inline constexpr char kBadBinaryProto[] = "Unable to decode binary proto";
inline constexpr char kBadCompressedBuyerInput[] =
    "Unable to decompress buyer input for buyer: %s";
inline constexpr char kBadBuyerInputProto[] =
    "Unable to decode BuyerInput binary proto for buyer: %s";
inline constexpr char kPASSignalsForComponentAuction[] =
    "Unsupported component auction input (protected signals) for buyer: %s";
inline constexpr char kMalformedBuyerInput[] = "Malformed buyer input.";
inline constexpr char kEmptySelectAdRequest[] =
    "Empty SelectAdRequest received.";
inline constexpr char kEmptyProtectedAuctionCiphertextError[] =
    "protected_auction_ciphertext must be non-null.";
inline constexpr char kUnsupportedClientType[] = "Unsupported client type.";
inline constexpr char kErrorDecryptingCiphertextError[] =
    "Error while decrypting protected_auction_ciphertext: %s.";
inline constexpr char kErrorDecryptingAuctionResultError[] =
    "Error while decrypting auction_result_ciphertext: %s.";
inline constexpr char kErrorDecodingAuctionResultError[] =
    "Error while decoding component auction_result: %s.";
inline constexpr char kErrorInAuctionResult[] =
    "Error while validating component auction_result: %s.";
inline constexpr char kMismatchedGenerationIdInAuctionResultError[] =
    "Mismatched generationId.";
inline constexpr char kEmptyComponentSellerInAuctionResultError[] =
    "Component Seller Auction Result must contain component seller domain";
inline constexpr char kMismatchedTopLevelSellerInAuctionResultError[] =
    "Mismatched top level seller.";
inline constexpr char kUnsupportedAdTypeInAuctionResultError[] =
    "Unsupported ad type.";
inline constexpr char kTopLevelWinReportingUrlsInAuctionResultError[] =
    "Top Level Win Reporting URLs should not be present.";
inline constexpr char kMultipleComponentAuctionResultsError[] =
    "Top Level Auction contains multiple auction results for the same seller: ";
inline constexpr absl::string_view kMismatchedCurrencyInAuctionResultError =
    "Component seller auction result bid currency does not match top-level "
    "seller's expected currency for this component seller.";

// Server side errors listed here.
inline constexpr char kInternalError[] = "Internal Error";
inline constexpr char kEncryptionFailed[] = "Encryption Failure.";
inline constexpr char kRequestCancelled[] = "Request Cancelled by Client.";

// Constants for bad Ad server provided inputs.
inline constexpr char kEmptySellerSignals[] =
    "Seller signals missing in auction config";
inline constexpr char kEmptyAuctionSignals[] =
    "Auction signals missing in auction config";
inline constexpr char kEmptyBuyerList[] = "No buyers specified";
inline constexpr char kEmptySeller[] =
    "Seller origin missing in auction config";
inline constexpr char kEmptyBuyerSignals[] =
    "Buyer signals missing in auction config for buyer: %s";
inline constexpr char kWrongSellerDomain[] =
    "Seller domain passed in request does not match this server's domain";
inline constexpr char kEmptyBuyerInPerBuyerConfig[] =
    "One or more buyer keys are empty in per buyer config map";
inline constexpr absl::string_view kEmptySellerInPerComponentSellerConfig =
    "One or more seller keys are empty in per component seller config map";
inline constexpr char kDeviceComponentAuctionWithAndroid[] =
    "Device orchestrated Component Auctions not supported for Android";
inline constexpr char kNoComponentAuctionResults[] =
    "No Component Auction Results for Top Level Seller auction";
inline constexpr char kEmptyComponentAuctionResults[] =
    "Empty Component Auction Results for Top Level Seller auction";
inline constexpr absl::string_view kInvalidExpectedComponentSellerCurrency =
    "Invalid Expected Component Seller Currency";

// Error handling related constants.
inline constexpr char kErrorDelimiter[] = "; ";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_ERROR_CATEGORIES_H_
