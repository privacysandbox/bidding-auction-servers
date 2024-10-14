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

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/util/data_util.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {

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
inline constexpr char kProtectedAuctionInput[] = "ProtectedAuctionInput";
inline constexpr char kBrowserSignalsKey[] = "browserSignals[x].key";
inline constexpr char kBrowserSignalsBidCount[] = "browserSignals[x].bidCount";
inline constexpr char kBrowserSignalsJoinCount[] =
    "browserSignals[x].joinCount";
inline constexpr char kBrowserSignalsRecency[] = "browserSignals[x].recency";
inline constexpr char kBrowserSignalsRecencyMs[] =
    "browserSignals[x].recencyMs";
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
inline constexpr char kAdComponent[] = "interestGroups.component";
inline constexpr char kAdComponentEntry[] = "interestGroups.component[x]";

inline constexpr char kConsentedDebugConfigKey[] = "consentedDebugConfig.key";
inline constexpr char kConsentedDebugConfigIsConsented[] =
    "consentedDebugConfig.isConsented";
inline constexpr char kConsentedDebugConfigToken[] =
    "consentedDebugConfig.token";
inline constexpr char kConsentedDebugConfigIsDebugInfoInResponse[] =
    "consentedDebugConfig.isDebugInfoInResponse";

// Constants for data types in CBOR payload
inline constexpr char kString[] = "string";
inline constexpr char kArray[] = "array";
inline constexpr char kBool[] = "bool";
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

#define RETURN_IF_PREV_ERRORS(error_accumulator, should_fail_fast, to_return) \
  if ((error_accumulator).HasErrors() && (should_fail_fast)) {                \
    return to_return;                                                         \
  }

// Data related to k-anon winner/ghost winner that is to be populated in the
// AuctionResult returned to the client.
struct KAnonAuctionResultData {
  std::vector<AuctionResult::KAnonGhostWinner> kanon_ghost_winners;
  AuctionResult::KAnonJoinCandidate kanon_winner_join_candidates;
  int kanon_winner_positional_index = -1;
};

// Encodes the data into a CBOR-serialized AuctionResult response for single
// seller auction.
absl::StatusOr<std::string> Encode(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const ::google::protobuf::Map<
        std::string, AuctionResult::InterestGroupIndex>& bidding_group_map,
    const UpdateGroupMap& update_group_map,
    const std::optional<AuctionResult::Error>& error,
    const std::function<void(const grpc::Status&)>& error_handler,
    const KAnonAuctionResultData* kanon_auction_result_data = nullptr);

// Encodes the data into a CBOR-serialized AuctionResult response for component
// seller auction.
absl::StatusOr<std::string> EncodeComponent(
    absl::string_view top_level_seller,
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const ::google::protobuf::Map<
        std::string, AuctionResult::InterestGroupIndex>& bidding_group_map,
    const UpdateGroupMap& update_group_map,
    const std::optional<AuctionResult::Error>& error,
    const std::function<void(const grpc::Status&)>& error_handler);

// Helper to validate the type of a CBOR object.
bool IsTypeValid(
    absl::AnyInvocable<bool(const cbor_item_t*)> is_valid_type,
    const cbor_item_t* item, absl::string_view field_name,
    absl::string_view expected_type, ErrorAccumulator& error_accumulator,
    server_common::SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG);

// Reads a cbor item into a string. Caller must verify that the item is a string
// before calling this method.
std::string DecodeCborString(const cbor_item_t* item);

// Decodes the key (i.e. owner) in the BuyerInputs in ProtectedAudienceInput
// and copies the corresponding value (i.e. BuyerInput) as-is. Note: this method
// doesn't decode the value.
::google::protobuf::Map<std::string, std::string> DecodeBuyerInputKeys(
    cbor_item_t& compressed_encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast = true);

// Decodes consented debug config object.
server_common::ConsentedDebugConfiguration DecodeConsentedDebugConfig(
    const cbor_item_t* root, ErrorAccumulator& error_accumulator,
    bool fail_fast);

template <typename T>
T DecodeProtectedAuctionInput(cbor_item_t* root,
                              ErrorAccumulator& error_accumulator,
                              bool fail_fast) {
  T output;

  IsTypeValid(&cbor_isa_map, root, kProtectedAuctionInput, kMap,
              error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator, /*fail_fast=*/true, output);

  absl::Span<cbor_pair> entries(cbor_map_handle(root), cbor_map_size(root));
  for (const cbor_pair& entry : entries) {
    bool is_valid_key_type = IsTypeValid(
        &cbor_isa_string, entry.key, kRootCborKey, kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);
    if (!is_valid_key_type) {
      continue;
    }

    const int index =
        FindItemIndex(kRequestRootKeys, DecodeCborString(entry.key));
    switch (index) {
      case 0: {  // Schema version.
        bool is_valid_schema_type = IsTypeValid(
            &cbor_is_int, entry.value, kVersion, kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);

        // Only support version 0 schemas for now.
        if (is_valid_schema_type && cbor_get_int(entry.value) != 0) {
          const std::string error = absl::StrFormat(
              kUnsupportedSchemaVersionError, cbor_get_int(entry.value));
          error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                                        ErrorCode::CLIENT_SIDE);
        }
        break;
      }
      case 1: {  // Publisher.
        bool is_valid_publisher_type =
            IsTypeValid(&cbor_isa_string, entry.value, kPublisher, kString,
                        error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);

        if (is_valid_publisher_type) {
          output.set_publisher_name(DecodeCborString(entry.value));
        }
        break;
      }
      case 2: {  // Interest groups.
        *output.mutable_buyer_input() =
            DecodeBuyerInputKeys(*entry.value, error_accumulator, fail_fast);
        break;
      }
      case 3: {  // Generation Id.
        bool is_valid_gen_type =
            IsTypeValid(&cbor_isa_string, entry.value, kGenerationId, kString,
                        error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);

        if (is_valid_gen_type) {
          output.set_generation_id(DecodeCborString(entry.value));
        }
        break;
      }
      case 4: {  // Enable Debug Reporting.
        bool is_valid_debug_type =
            IsTypeValid(&cbor_is_bool, entry.value, kDebugReporting, kString,
                        error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);

        if (is_valid_debug_type) {
          output.set_enable_debug_reporting(cbor_get_bool(entry.value));
        }
        break;
      }
      case 5: {  // Consented Debug Config.
        *output.mutable_consented_debug_config() = DecodeConsentedDebugConfig(
            entry.value, error_accumulator, fail_fast);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);
        break;
      }
      case 6: {  // Request timestamp ms.
        bool is_valid_timestamp_type =
            IsTypeValid(&cbor_is_int, entry.value, kRequestTimestampMs, kInt,
                        error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);

        if (is_valid_timestamp_type) {
          output.set_request_timestamp_ms(cbor_get_uint64(entry.value));
        }
        break;
      }
      case 7: {  // Enforce k-Anon.
        bool is_valid_enforce_k_anon_type =
            IsTypeValid(&cbor_is_bool, entry.value, kEnforceKAnon, kBool,
                        error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, output);

        if (is_valid_enforce_k_anon_type) {
          output.set_enforce_kanon(cbor_get_bool(entry.value));
        }
        break;
      }
      default:
        break;
    }
  }

  return output;
}

// Decodes CBOR-encoded ProtectedAudienceInput. Note that this method doesn't
// decompress and decodes the BuyerInput values in the buyer input map. Any
// errors are reported to `error_accumulator`.
template <typename T>
T Decode(absl::string_view cbor_payload, ErrorAccumulator& error_accumulator,
         bool fail_fast = true) {
  T protected_auction_input;
  cbor_load_result result;
  ScopedCbor root(
      cbor_load(reinterpret_cast<const unsigned char*>(cbor_payload.data()),
                cbor_payload.size(), &result));
  if (result.error.code != CBOR_ERR_NONE) {
    error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE,
                                  kInvalidCborError, ErrorCode::CLIENT_SIDE);
    return protected_auction_input;
  }

  return DecodeProtectedAuctionInput<T>(*root, error_accumulator, fail_fast);
}

// Serializes the bidding groups (buyer origin => interest group indices map)
// to CBOR. Note: this should not be used directly and is only here to
// facilitate testing.
absl::Status CborSerializeBiddingGroups(
    const google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>&
        bidding_groups,
    const std::function<void(const grpc::Status&)>& error_handler,
    cbor_item_t& root);

// Serializes the interest groups groups to CBOR. This structure is represented
// by the following CDDL:
// updateGroups = {
//   * interestGroupOwner => [* {
//        index : int,
//        updateIfOlderThanMs : int
//      }]
// }
// Note: this should not be used directly and is only here to
// facilitate testing.
absl::Status CborSerializeUpdateGroups(
    const UpdateGroupMap& update_groups,
    const std::function<void(const grpc::Status&)>& error_handler,
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
cbor_item_t* cbor_build_uint(uint input);

// Minimally encodes an int (positive or negative) into CBOR. Caller is
// responsible for decrementing the reference once done with the returned int.
cbor_item_t* cbor_build_int(int input);

// Minimally encodes a float into CBOR. Caller is responsible for decrementing
// the reference once done with the returned int.
absl::StatusOr<cbor_item_t*> cbor_build_float(double input);

// Checks for floats equality (subject to the system's limits).
template <typename T, typename U>
bool AreFloatsEqual(T a, U b) {
  bool result = std::fabs(a - b) < std::numeric_limits<double>::epsilon();
  PS_VLOG(6) << "a: " << a << ", b: " << b << ", diff: " << fabs(a - b)
             << ", EPS: " << std::numeric_limits<double>::epsilon()
             << ", floats equal: " << result;
  return result;
}

// Serializes WinReportingUrls for buyer and seller.
absl::Status CborSerializeWinReportingUrls(
    const WinReportingUrls& win_reporting_urls,
    const std::function<void(const grpc::Status&)>& error_handler,
    cbor_item_t& root);

// Decodes cbor string input to std::string
inline std::string CborDecodeString(cbor_item_t* input) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(input)),
                     cbor_string_length(input));
}

inline constexpr std::array<std::string_view, kNumAuctionResultKeys>
    kAuctionResultKeys = {
        kScore,                       // 0
        kBid,                         // 1
        kChaff,                       // 2
        kAdRenderUrl,                 // 3
        kBiddingGroups,               // 4
        kInterestGroupName,           // 5
        kInterestGroupOwner,          // 6
        kAdComponents,                // 7
        kError,                       // 8
        kWinReportingUrls,            // 9
        kAdMetadata,                  // 10
        kTopLevelSeller,              // 11
        kBidCurrency,                 // 12
        kBuyerReportingId,            // 13
        kKAnonGhostWinners,           // 14
        kKAnonWinnerJoinCandidates,   // 15
        kKAnonWinnerPositionalIndex,  // 16
        kUpdateGroups                 // 17
};

template <std::size_t Size>
int FindKeyIndex(const std::array<absl::string_view, Size>& haystack,
                 absl::string_view needle) {
  auto it = std::find(haystack.begin(), haystack.end(), needle);
  if (it == haystack.end()) {
    return -1;
  }
  return std::distance(haystack.begin(), it);
}

// Decodes the CBOR-serialized AuctionResult to proto.
absl::StatusOr<AuctionResult> CborDecodeAuctionResultToProto(
    absl::string_view serialized_input);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_WEB_UTILS_H_
