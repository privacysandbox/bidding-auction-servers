/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_WEB_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_WEB_UTILS_H_

#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/util/error_accumulator.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {

using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;

// Client facing error messages.
inline constexpr char kInvalidCborError[] =
    "Unable to parse ProtectedAudienceInput CBOR";
inline constexpr char kInvalidTypeError[] =
    "Invalid type provided for (field: %s, expected: %s, "
    "actual: %s)";
inline constexpr char kUnsupportedSchemaVersionError[] =
    "Unsupported schema version provided: (provided: %d)";
inline constexpr char kUnrecognizedProtectedAudienceKeyError[] =
    "Unrecognized key found while parsing ProtectedAudienceInput: (%s)";
inline constexpr char kMalformedCompressedBytestring[] =
    "Malformed bytestring for compressed interest group";
inline constexpr char kInvalidBuyerInputCborError[] =
    "Invalid BuyerInput CBOR document for buyer: %s";
inline constexpr char kPrevWinsNotCorrectLengthError[] =
    "Found browserSignals.prevWins[x] array of not length 2 for owner '%s'";
inline constexpr char kMalformattedBrowserSignalsError[] =
    "Malformed browser signals object: %s";

// CBOR types
inline constexpr char kCborTypePositiveInt[] = "positive int";
inline constexpr char kCborTypeNegativeInt[] = "negative int";
inline constexpr char kCborTypeByteString[] = "bytestring";
inline constexpr char kCborTypeString[] = "string";
inline constexpr char kCborTypeArray[] = "array";
inline constexpr char kCborTypeMap[] = "map";
inline constexpr char kCborTypeTag[] = "tag";
inline constexpr char kCborTypeFloatControl[] = "float control";

// Constants for fields returned to clients in error messages.
inline constexpr char kUnknownDataType[] = "unknown";
inline constexpr char kRootCborKey[] = "Root level CBOR key";
inline constexpr char kProtectedAudienceInput[] = "ProtectedAudienceInput";
inline constexpr char kBrowserSignalsKey[] = "browserSignals[x].key";
inline constexpr char kBrowserSignalsBidCount[] = "browserSignals[x].bidCount";
inline constexpr char kBrowserSignalsJoinCount[] =
    "browserSignals[x].joinCount";
inline constexpr char kBrowserSignalsRecency[] = "browserSignals[x].recency";
inline constexpr char kBrowserSignalsPrevWins[] = "browserSignals[x].prevWins";
inline constexpr char kPrevWinsEntry[] = "browserSignals[x].prevWins[y]";
inline constexpr char kPrevWinsTimeEntry[] = "browserSignals[x].prevWins[y][0]";
inline constexpr char kPrevWinsAdRenderIdEntry[] =
    "browserSignals[x].prevWins[y][1]";
inline constexpr char kIgKey[] = "interestGroups.key";
inline constexpr char kIgValue[] = "interestGroups.value";
inline constexpr char kIgName[] = "interestGroups.name";
inline constexpr char kIgBiddingSignalKeys[] =
    "interestGroups.biddingSignalKeys";
inline constexpr char kIgBiddingSignalKeysEntry[] =
    "interestGroups.biddingSignalKeys[x]";
inline constexpr char kBuyerInput[] = "BuyerInput";
inline constexpr char kBuyerInputEntry[] = "BuyerInput[x]";
inline constexpr char kBuyerInputKey[] = "BuyerInput[x].key";
inline constexpr char kAdComponent[] = "interestGroups.adComponent";
inline constexpr char kAdComponentEntry[] = "interestGroups.adComponent[x]";

inline constexpr char kConsentedDebugConfigKey[] = "consentedDebugConfig.key";
inline constexpr char kConsentedDebugConfigIsConsented[] =
    "consentedDebugConfig.isConsented";
inline constexpr char kConsentedDebugConfigToken[] =
    "consentedDebugConfig.token";

// Constants for data types in CBOR payload
inline constexpr char kString[] = "string";
inline constexpr char kArray[] = "array";
inline constexpr char kMap[] = "map";
inline constexpr char kInt[] = "int";
inline constexpr char kByteString[] = "bytestring";

// Misc constants
inline constexpr char kSuccessfulCborDecode[] = "CBOR successfully decoded";
inline constexpr char kSuccessfulBuyerInputDecode[] =
    "Successfully decoded BuyerInput for owner '%s'";
inline constexpr char kMalformedCompressedIgError[] =
    "Malformed bytestring for compressed interest group for buyer: %s";

// Comparator to order strings by length first and then lexicographically.
inline constexpr auto kComparator = [](absl::string_view a,
                                       absl::string_view b) {
  if (a.size() == b.size()) {
    return a < b;
  }
  return a.size() < b.size();
};

// Encodes the data into a CBOR-serialized AuctionResult response.
absl::StatusOr<std::string> Encode(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const ::google::protobuf::Map<
        std::string, AuctionResult::InterestGroupIndex>& bidding_group_map,
    std::optional<AuctionResult::Error> error,
    const std::function<void(absl::string_view)>& error_handler);

// Decodes CBOR-encoded ProtectedAudienceInput. Note that this method doesn't
// decompress and decodes the BuyerInput values in the buyer input map. Any
// errors are reported to `error_accumulator`.
ProtectedAudienceInput Decode(absl::string_view cbor_payload,
                              ErrorAccumulator& error_accumulator,
                              bool fail_fast = true);

// Serializes the bidding groups (buyer origin => interest group indices map)
// to CBOR. Note: this should not be used directly and is only here to
// facilitate testing.
absl::Status CborSerializeBiddingGroups(
    const google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>&
        bidding_groups,
    const std::function<void(absl::string_view)>& error_handler,
    cbor_item_t& root);

// Decodes the decompressed but CBOR encoded BuyerInput map to a mapping from
// owner => BuyerInput. Errors are reported to `error_accumulator`.
absl::flat_hash_map<absl::string_view, BuyerInput> DecodeBuyerInputs(
    const google::protobuf::Map<std::string, std::string>& encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast = true);

// Decompresses and the decodes the CBOR encoded and compressed BuyerInput.
// Errors are reported to `error_accumulator`.
BuyerInput DecodeBuyerInput(absl::string_view owner,
                            absl::string_view compressed_buyer_input,
                            ErrorAccumulator& error_accumulator,
                            bool fail_fast = true);

// Minimally encodes an unsigned int into CBOR. Caller is responsible for
// decrementing the reference once done with the returned int.
cbor_item_t* cbor_build_uint(uint32_t input);

// Minimally encodes a float into CBOR. Caller is responsible for decrementing
// the reference once done with the returned int.
absl::StatusOr<cbor_item_t*> cbor_build_float(double input);

// Checks for floats equality (subject to the system's limits).
template <typename T, typename U>
bool AreFloatsEqual(T a, U b) {
  bool result = std::fabs(a - b) < std::numeric_limits<double>::epsilon();
  if (VLOG_IS_ON(6)) {
    VLOG(6) << "a: " << a << ", b: " << b << ", diff: " << fabs(a - b)
            << ", EPS: " << std::numeric_limits<double>::epsilon()
            << ", floats equal: " << result;
  }
  return result;
}

// Serializes WinReportingUrls for buyer and seller.
absl::Status CborSerializeWinReportingUrls(
    const WinReportingUrls& win_reporting_urls,
    const std::function<void(absl::string_view)>& error_handler,
    cbor_item_t& root);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_WEB_UTILS_H_
