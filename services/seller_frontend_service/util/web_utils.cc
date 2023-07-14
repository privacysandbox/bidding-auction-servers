// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/seller_frontend_service/util/web_utils.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"
#include "services/common/util/status_macros.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

#define RETURN_IF_PREV_ERRORS(error_accumulator, should_fail_fast, to_return) \
  if (error_accumulator.HasErrors() && should_fail_fast) {                    \
    return to_return;                                                         \
  }

using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using ErrorHandler = const std::function<void(absl::string_view)>&;
using RepeatedStringProto = ::google::protobuf::RepeatedPtrField<std::string>;

// Array for mapping from the CBOR data type enum (a number) to a concrete data
// type. Used for returning helpful error messages when clients incorrectly
// construct the CBOR payload. For the ordering of the data types, see:
// https://github.com/PJK/libcbor/blob/master/src/cbor/data.h
inline constexpr int kNumCborDataTypes = 8;
inline constexpr std::array<absl::string_view, kNumCborDataTypes>
    kCborDataTypesLookup = {
        kCborTypePositiveInt,   // CBOR_TYPE_UINT (positive integers)
        kCborTypeNegativeInt,   // CBOR_TYPE_NEGINT (negative integers)
        kCborTypeByteString,    // CBOR_TYPE_BYTESTRING
        kCborTypeString,        // CBOR_TYPE_STRING
        kCborTypeArray,         // CBOR_TYPE_ARRAY
        kCborTypeMap,           // CBOR_TYPE_MAP
        kCborTypeTag,           // CBOR_TYPE_TAG
        kCborTypeFloatControl,  // CBOR_TYPE_FLOAT_CTRL ("decimals and special
                                // values")
};

template <std::size_t Size>
int FindItemIndex(const std::array<absl::string_view, Size>& haystack,
                  absl::string_view needle) {
  auto it = std::find(haystack.begin(), haystack.end(), needle);
  if (it == haystack.end()) {
    return -1;
  }

  return std::distance(haystack.begin(), it);
}

// Helper to validate the type of a CBOR object.
bool IsTypeValid(absl::AnyInvocable<bool(const cbor_item_t*)> is_valid_type,
                 const cbor_item_t* item, absl::string_view field_name,
                 absl::string_view expected_type,
                 ErrorAccumulator& error_accumulator,
                 SourceLocation location PS_LOC_CURRENT_DEFAULT_ARG) {
  if (!is_valid_type(item)) {
    absl::string_view actual_type = kUnknownDataType;
    if (item->type < kCborDataTypesLookup.size()) {
      actual_type = kCborDataTypesLookup[item->type];
    }

    std::string error = absl::StrFormat(kInvalidTypeError, field_name,
                                        expected_type, actual_type);
    VLOG(3) << "CBOR type validation failure at: " << location.file_name()
            << ":" << location.line();
    error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                                  ErrorCode::CLIENT_SIDE);
    return false;
  }

  return true;
}

// Reads a cbor item into a string. Caller must verify that the item is a string
// before calling this method.
std::string DecodeCborString(const cbor_item_t* item) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(item)),
                     cbor_string_length(item));
}

// Decodes a Span of cbor* string objects and adds them to the provided list.
RepeatedStringProto DecodeStringArray(absl::Span<cbor_item_t*> span,
                                      absl::string_view field_name,
                                      ErrorAccumulator& error_accumulator,
                                      bool fail_fast) {
  RepeatedStringProto repeated_field;
  for (const cbor_item_t* ad : span) {
    bool is_valid = IsTypeValid(&cbor_isa_string, ad, field_name, kString,
                                error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, repeated_field);
    if (is_valid) {
      repeated_field.Add(DecodeCborString(ad));
    }
  }

  return repeated_field;
}

// Collects the prevWins arrays into a JSON array and stringifies the result.
absl::StatusOr<std::string> GetStringifiedPrevWins(
    absl::Span<cbor_item_t*> prev_wins_entries, absl::string_view owner,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  rapidjson::Document document;
  document.SetArray();
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

  // Previous win entries should be in the form [relative_time, ad_render_id]
  // where relative_time is an int and ad_render_id is a string.
  for (const cbor_item_t* prev_win : prev_wins_entries) {
    bool is_valid = IsTypeValid(&cbor_isa_array, prev_win, kPrevWinsEntry,
                                kArray, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");
    if (!is_valid) {
      continue;
    }

    if (cbor_array_size(prev_win) != 2) {
      const std::string error =
          absl::StrFormat(kPrevWinsNotCorrectLengthError, owner);
      error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                                    ErrorCode::CLIENT_SIDE);
      RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");
    }

    // cbor_array_get calls cbor_incref() on the returned object, so call
    // cbor_move().
    ScopedCbor relative_time(cbor_array_get(prev_win, kRelativeTimeIndex));
    IsTypeValid(&cbor_is_int, *relative_time, kPrevWinsTimeEntry, kInt,
                error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");

    ScopedCbor maybe_ad_render_id(cbor_array_get(prev_win, kAdRenderIdIndex));
    IsTypeValid(&cbor_isa_string, *maybe_ad_render_id, kPrevWinsAdRenderIdEntry,
                kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, "");

    if (error_accumulator.HasErrors()) {
      // No point in processing invalid data but we may continue to validate the
      // input.
      continue;
    }

    const int time = cbor_get_int(*relative_time);
    const std::string ad_render_id = DecodeCborString(*maybe_ad_render_id);

    // Convert to JSON array and add to the running JSON document.
    rapidjson::Value array(rapidjson::kArrayType);
    array.PushBack(time, allocator);
    rapidjson::Value ad_render_id_value(rapidjson::kStringType);
    ad_render_id_value.SetString(ad_render_id.c_str(), ad_render_id.length(),
                                 allocator);
    array.PushBack(ad_render_id_value, allocator);
    document.PushBack(array, allocator);
  }

  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);
  document.Accept(writer);
  return string_buffer.GetString();
}

// Decodes browser signals object and sets it in the 'buyer_interest_group'.
BrowserSignals DecodeBrowserSignals(const cbor_item_t* root,
                                    absl::string_view owner,
                                    ErrorAccumulator& error_accumulator,
                                    bool fail_fast) {
  BrowserSignals signals;
  bool is_signals_valid_type = IsTypeValid(&cbor_isa_map, root, kBrowserSignals,
                                           kMap, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator, /*fail_fast=*/!is_signals_valid_type,
                        signals);

  absl::Span<cbor_pair> browser_signal_entries(cbor_map_handle(root),
                                               cbor_map_size(root));
  for (const cbor_pair& signal : browser_signal_entries) {
    bool is_valid_key_type =
        IsTypeValid(&cbor_isa_string, signal.key, kBrowserSignalsKey, kString,
                    error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
    if (!is_valid_key_type) {
      continue;
    }

    const int index =
        FindItemIndex(kBrowserSignalKeys, DecodeCborString(signal.key));
    switch (index) {
      case 0: {  // Bid count.
        bool is_count_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsBidCount,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_count_valid_type) {
          signals.set_bid_count(cbor_get_int(signal.value));
        }
        break;
      }
      case 1: {  // Join count.
        bool is_count_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsJoinCount,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_count_valid_type) {
          signals.set_join_count(cbor_get_int(signal.value));
        }
        break;
      }
      case 2: {  // Recency.
        bool is_recency_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsRecency,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_recency_valid_type) {
          signals.set_recency(cbor_get_int(signal.value));
        }
        break;
      }
      case 3: {  // Previous wins.
        bool is_win_valid_type =
            IsTypeValid(&cbor_isa_array, signal.value, kBrowserSignalsPrevWins,
                        kArray, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_win_valid_type) {
          absl::Span<cbor_item_t*> prev_wins_entries(
              cbor_array_handle(signal.value), cbor_array_size(signal.value));
          absl::StatusOr<std::string> prev_wins = GetStringifiedPrevWins(
              prev_wins_entries, owner, error_accumulator, fail_fast);
          *signals.mutable_prev_wins() = std::move(*prev_wins);
        }
        break;
      }
    }
  }

  return signals;
}

// Decodes the key (i.e. owner) in the BuyerInputs in ProtectedAudienceInput
// and copies the corresponding value (i.e. BuyerInput) as-is. Note: this method
// doesn't decode the value.
EncodedBuyerInputs DecodeBuyerInputKeys(
    cbor_item_t& compressed_encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast = true) {
  EncodedBuyerInputs encoded_buyer_inputs;
  bool is_buyer_inputs_valid_type =
      IsTypeValid(&cbor_isa_map, &compressed_encoded_buyer_inputs,
                  kInterestGroups, kMap, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator,
                        /*fail_fast=*/!is_buyer_inputs_valid_type,
                        encoded_buyer_inputs);

  absl::Span<cbor_pair> interest_group_data_entries(
      cbor_map_handle(&compressed_encoded_buyer_inputs),
      cbor_map_size(&compressed_encoded_buyer_inputs));
  for (const cbor_pair& interest_group : interest_group_data_entries) {
    bool is_ig_key_valid_type =
        IsTypeValid(&cbor_isa_string, interest_group.key, kIgKey, kString,
                    error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, encoded_buyer_inputs);

    if (!is_ig_key_valid_type) {
      continue;
    }

    const std::string owner = DecodeCborString(interest_group.key);
    // The value is a gzip compressed bytestring.
    bool is_ig_val_valid_type =
        IsTypeValid(&cbor_isa_bytestring, interest_group.value, kIgValue,
                    kByteString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, encoded_buyer_inputs);

    if (!is_ig_val_valid_type) {
      continue;
    }
    const std::string compressed_igs(
        reinterpret_cast<char*>(cbor_bytestring_handle(interest_group.value)),
        cbor_bytestring_length(interest_group.value));
    encoded_buyer_inputs.insert({std::move(owner), std::move(compressed_igs)});
  }

  return encoded_buyer_inputs;
}

google::protobuf::Map<std::string, BuyerInput> DecodeInterestGroupEntries(
    absl::Span<cbor_pair> interest_group_data_entries,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  google::protobuf::Map<std::string, BuyerInput> result;
  for (const cbor_pair& interest_group : interest_group_data_entries) {
    bool is_valid_key_type = IsTypeValid(&cbor_isa_string, interest_group.key,
                                         kIgKey, kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, result);

    if (!is_valid_key_type) {
      continue;
    }

    const std::string owner = DecodeCborString(interest_group.key);
    // The value is a gzip compressed bytestring.
    bool is_owner_valid_type =
        IsTypeValid(&cbor_isa_bytestring, interest_group.value, kIgValue,
                    kByteString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, result);

    if (!is_owner_valid_type) {
      continue;
    }

    const std::string compressed_igs(
        reinterpret_cast<char*>(cbor_bytestring_handle(interest_group.value)),
        cbor_bytestring_length(interest_group.value));
    const absl::StatusOr<std::string> decompressed_buyer_input =
        GzipDecompress(compressed_igs);
    if (!decompressed_buyer_input.ok()) {
      error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE,
                                    kMalformedCompressedBytestring,
                                    ErrorCode::CLIENT_SIDE);
    }
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, result);

    BuyerInput buyer_input = DecodeBuyerInput(owner, *decompressed_buyer_input,
                                              error_accumulator, fail_fast);

    result.insert({std::move(owner), std::move(buyer_input)});
  }

  return result;
}

ProtectedAudienceInput DecodeProtectedAudienceInput(
    cbor_item_t* root, ErrorAccumulator& error_accumulator, bool fail_fast) {
  ProtectedAudienceInput output;

  IsTypeValid(&cbor_isa_map, root, kProtectedAudienceInput, kMap,
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
      default:
        break;
    }
  }

  return output;
}

absl::Status CborSerializeString(absl::string_view key, absl::string_view value,
                                 ErrorHandler error_handler,
                                 cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_stringn(value.data(), value.size()))};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeFloat(absl::string_view key, float value,
                                ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_float8(value))};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeBool(absl::string_view key, bool value,
                               ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_bool(value))};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeAdComponentUrls(
    absl::string_view key, const RepeatedStringProto& component_renders,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_component_renders(
      cbor_new_definite_array(component_renders.size()));
  for (const auto& component_render : component_renders) {
    if (!cbor_array_push(
            *serialized_component_renders,
            cbor_move(cbor_build_stringn(component_render.data(),
                                         component_render.size())))) {
      error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
      return absl::InternalError("");
    }
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_component_renders};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", key, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeScoreAdResponse(
    const ScoreAdsResponse::AdScore& ad_score, ErrorHandler error_handler,
    cbor_item_t& root) {
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kScore, ad_score.desirability(), error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kBid, ad_score.buyer_bid(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeBool(kChaff, false, error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kAdRenderUrl, ad_score.render(),
                                         error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kInterestGroupName, ad_score.interest_group_name(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kInterestGroupOwner,
                                         ad_score.interest_group_owner(),
                                         error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeAdComponentUrls(
      kAdComponents, ad_score.component_renders(), error_handler, root));
  return absl::OkStatus();
}

absl::StatusOr<std::vector<unsigned char>> GetCborSerializedAuctionResult(
    ErrorHandler error_handler, cbor_item_t& cbor_data_root) {
  absl::Status internal_error_status = absl::InternalError("");
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size =
      cbor_serialized_size(&cbor_data_root);
  if (!cbor_serialized_data_size) {
    error_handler(
        "Failed to serialize the AuctionResult to CBOR (data is too large!)");
    return absl::InternalError("");
  }

  std::vector<unsigned char> byte_string(cbor_serialized_data_size);
  if (cbor_serialize(&cbor_data_root, byte_string.data(),
                     cbor_serialized_data_size) == 0) {
    error_handler("Failed to serialize the AuctionResult to CBOR");
    return absl::InternalError("");
  }
  return byte_string;
}

absl::Status CborSerializeError(const AuctionResult::Error& error,
                                ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_error_map(cbor_new_definite_map(kNumErrorKeys));
  const std::string& message = error.message();
  struct cbor_pair message_kv = {
      .key = cbor_move(cbor_build_stringn(kMessage, sizeof(kMessage) - 1)),
      .value = cbor_move(cbor_build_stringn(message.data(), message.size()))};
  if (!cbor_map_add(*serialized_error_map, std::move(message_kv))) {
    error_handler(
        absl::StrCat("Failed to serialize error ", kMessage, " to CBOR"));
    return absl::InternalError("");
  }

  struct cbor_pair code_kv = {
      .key = cbor_move(cbor_build_stringn(kCode, sizeof(kCode) - 1)),
      .value = cbor_move(cbor_build_uint32(error.code()))};
  if (!cbor_map_add(*serialized_error_map, std::move(code_kv))) {
    error_handler(
        absl::StrCat("Failed to serialize error ", kCode, " to CBOR"));
    return absl::InternalError("");
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kError, sizeof(kError) - 1)),
      .value = *serialized_error_map};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(absl::StrCat("Failed to serialize ", kError, " to CBOR"));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeBiddingGroups(BiddingGroupMap bidding_groups,
                                        ErrorHandler error_handler,
                                        cbor_item_t& root) {
  ScopedCbor serialized_group_map(cbor_new_definite_map(bidding_groups.size()));
  for (const auto& [origin, group_indices] : bidding_groups) {
    ScopedCbor serialized_group_indices(
        cbor_new_definite_array(group_indices.index_size()));
    for (int32_t index : group_indices.index()) {
      if (!cbor_array_push(*serialized_group_indices,
                           cbor_move(cbor_build_uint32(index)))) {
        error_handler("Failed to serialize a bidding group index to CBOR");
        return absl::InternalError("");
      }
    }
    struct cbor_pair kv = {
        .key = cbor_move(cbor_build_stringn(origin.c_str(), origin.size())),
        .value = *serialized_group_indices};
    if (!cbor_map_add(*serialized_group_map, std::move(kv))) {
      error_handler(
          "Failed to serialize an <origin, bidding group array> pair to CBOR");
      return absl::InternalError("");
    }
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kBiddingGroups, sizeof(kBiddingGroups) - 1)),
                         .value = *serialized_group_map};
  if (!cbor_map_add(&root, std::move(kv))) {
    error_handler(
        absl::StrCat("Failed to serialize ", kBiddingGroups, " to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

}  // namespace

ProtectedAudienceInput Decode(absl::string_view cbor_payload,
                              ErrorAccumulator& error_accumulator,
                              bool fail_fast) {
  ProtectedAudienceInput protected_audience_input;
  cbor_load_result result;
  ScopedCbor root(
      cbor_load(reinterpret_cast<const unsigned char*>(cbor_payload.data()),
                cbor_payload.size(), &result));
  if (result.error.code != CBOR_ERR_NONE) {
    error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE,
                                  kInvalidCborError, ErrorCode::CLIENT_SIDE);
    return protected_audience_input;
  }

  protected_audience_input =
      DecodeProtectedAudienceInput(*root, error_accumulator, fail_fast);
  return protected_audience_input;
}

absl::StatusOr<std::vector<unsigned char>> Encode(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupMap& bidding_group_map,
    std::optional<AuctionResult::Error> error, ErrorHandler error_handler) {
  absl::StatusOr<std::vector<unsigned char>> plain_response;
  // CBOR data's root handle. When serializing the auction result to CBOR, we
  // use this handle to keep the temporary data.
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumAuctionResultKeys));
  auto* cbor_internal = cbor_data_root.get();

  if (error.has_value()) {
    PS_RETURN_IF_ERROR(
        CborSerializeError(*error, error_handler, *cbor_internal));
    PS_ASSIGN_OR_RETURN(plain_response, GetCborSerializedAuctionResult(
                                            error_handler, *cbor_internal));
  } else if (high_score.has_value()) {
    PS_RETURN_IF_ERROR(CborSerializeScoreAdResponse(*high_score, error_handler,
                                                    *cbor_internal));
    PS_RETURN_IF_ERROR(CborSerializeBiddingGroups(
        bidding_group_map, error_handler, *cbor_internal));
  } else {
    PS_RETURN_IF_ERROR(
        CborSerializeBool(kChaff, true, error_handler, *cbor_internal));
  }

  PS_ASSIGN_OR_RETURN(plain_response, GetCborSerializedAuctionResult(
                                          error_handler, *cbor_internal));
  return std::move(*plain_response);
}

DecodedBuyerInputs DecodeBuyerInputs(
    const EncodedBuyerInputs& encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  DecodedBuyerInputs decoded_buyer_inputs;
  for (const auto& [owner, compressed_buyer_input] : encoded_buyer_inputs) {
    BuyerInput buyer_input = DecodeBuyerInput(owner, compressed_buyer_input,
                                              error_accumulator, fail_fast);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, decoded_buyer_inputs);

    decoded_buyer_inputs.insert({std::move(owner), std::move(buyer_input)});
  }

  return decoded_buyer_inputs;
}

BuyerInput DecodeBuyerInput(absl::string_view owner,
                            absl::string_view compressed_buyer_input,
                            ErrorAccumulator& error_accumulator,
                            bool fail_fast) {
  BuyerInput buyer_input;
  const absl::StatusOr<std::string> decompressed_buyer_input =
      GzipDecompress(compressed_buyer_input);
  if (!decompressed_buyer_input.ok()) {
    error_accumulator.ReportError(
        ErrorVisibility::CLIENT_VISIBLE,
        absl::StrFormat(kMalformedCompressedIgError, owner),
        ErrorCode::CLIENT_SIDE);
    return buyer_input;
  }

  cbor_load_result result;
  ScopedCbor root(cbor_load(
      reinterpret_cast<const unsigned char*>(decompressed_buyer_input->data()),
      decompressed_buyer_input->size(), &result));

  if (result.error.code != CBOR_ERR_NONE) {
    error_accumulator.ReportError(
        ErrorVisibility::CLIENT_VISIBLE,
        absl::StrFormat(kInvalidBuyerInputCborError, owner),
        ErrorCode::CLIENT_SIDE);
    return buyer_input;
  }

  bool is_buyer_input_valid_type = IsTypeValid(
      &cbor_isa_array, *root, kBuyerInput, kArray, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator,
                        /*fail_fast=*/!is_buyer_input_valid_type, buyer_input);

  absl::Span<cbor_item_t*> interest_groups(cbor_array_handle(*root),
                                           cbor_array_size(*root));
  for (const cbor_item_t* interest_group : interest_groups) {
    auto* buyer_interest_group = buyer_input.add_interest_groups();

    bool is_igs_valid_type =
        IsTypeValid(&cbor_isa_map, interest_group, kBuyerInputEntry, kMap,
                    error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

    if (!is_igs_valid_type) {
      continue;
    }

    absl::Span<cbor_pair> ig_entries(cbor_map_handle(interest_group),
                                     cbor_map_size(interest_group));
    for (const cbor_pair& ig_entry : ig_entries) {
      bool is_key_valid_type =
          IsTypeValid(&cbor_isa_string, ig_entry.key, kBuyerInputKey, kString,
                      error_accumulator);
      RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

      if (!is_key_valid_type) {
        continue;
      }

      const int index =
          FindItemIndex(kInterestGroupKeys, DecodeCborString(ig_entry.key));
      switch (index) {
        case 0: {  // Name.
          bool is_name_valid_type =
              IsTypeValid(&cbor_isa_string, ig_entry.value, kIgName, kString,
                          error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);
          if (is_name_valid_type) {
            buyer_interest_group->set_name(DecodeCborString(ig_entry.value));
          }
          break;
        }
        case 1: {  // Bidding signal keys.
          bool is_bs_valid_type =
              IsTypeValid(&cbor_isa_array, ig_entry.value, kIgBiddingSignalKeys,
                          kArray, error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_bs_valid_type) {
            absl::Span<cbor_item_t*> bidding_signals_list(
                cbor_array_handle(ig_entry.value),
                cbor_array_size(ig_entry.value));
            *buyer_interest_group->mutable_bidding_signals_keys() =
                DecodeStringArray(bidding_signals_list,
                                  kIgBiddingSignalKeysEntry, error_accumulator,
                                  fail_fast);
          }
          break;
        }
        case 2: {  // User bidding signals.
          bool is_bs_valid_type =
              IsTypeValid(&cbor_isa_string, ig_entry.value, kUserBiddingSignals,
                          kString, error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_bs_valid_type) {
            *buyer_interest_group->mutable_user_bidding_signals() =
                DecodeCborString(ig_entry.value);
          }
          break;
        }
        case 3: {  // Ad render IDs.
          bool is_ad_render_valid_type = IsTypeValid(
              &cbor_isa_array, ig_entry.value, kAds, kArray, error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_ad_render_valid_type) {
            absl::Span<cbor_item_t*> ads(cbor_array_handle(ig_entry.value),
                                         cbor_array_size(ig_entry.value));
            *buyer_interest_group->mutable_ad_render_ids() = DecodeStringArray(
                ads, kAdRenderId, error_accumulator, fail_fast);
          }
          break;
        }
        case 4: {  // Component ads.
          bool is_component_valid_type =
              IsTypeValid(&cbor_isa_array, ig_entry.value, kAdComponent, kArray,
                          error_accumulator);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);

          if (is_component_valid_type) {
            absl::Span<cbor_item_t*> component_ads(
                cbor_array_handle(ig_entry.value),
                cbor_array_size(ig_entry.value));
            *buyer_interest_group->mutable_component_ads() = DecodeStringArray(
                component_ads, kAdComponentEntry, error_accumulator, fail_fast);
          }
          break;
        }
        case 5: {  // Browser signals.
          *buyer_interest_group->mutable_browser_signals() =
              DecodeBrowserSignals(ig_entry.value, kIgBiddingSignalKeysEntry,
                                   error_accumulator, fail_fast);
          RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, buyer_input);
        }
      }
    }
  }

  return buyer_input;
}

}  // namespace privacy_sandbox::bidding_auction_servers
