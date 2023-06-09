// Copyright 2023 Google LLC
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

#include "services/common/test/utils/cbor_test_utils.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <rapidjson/error/en.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "cbor/strings.h"
#include "glog/logging.h"
#include "rapidjson/document.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"
#include "services/common/util/status_macros.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::RepeatedPtrField;
using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using BuyerInputMap = ::google::protobuf::Map<std::string, BuyerInput>;
using BuyerInputMapEncoded = ::google::protobuf::Map<std::string, std::string>;

inline constexpr std::array<std::string_view, kNumAuctionResultKeys>
    kAuctionResultKeys = {
        kScore,               // 0
        kBid,                 // 1
        kChaff,               // 2
        kAdRenderUrl,         // 3
        kBiddingGroups,       // 4
        kInterestGroupName,   // 5
        kInterestGroupOwner,  // 6
        kAdComponents,        // 7
        kError,               // 8
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

// Serializes the key and value pair and adds them to the provided map.
// Note that there could be multiple attributes associated to the value and
// hence is accepted as a parameter pack.
template <class T, class... ValueArgs>
absl::Status CborSerializeKeyValue(absl::string_view key, T invocable_builder,
                                   cbor_item_t& map, ValueArgs... value_args) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value =
          cbor_move(invocable_builder(std::forward<ValueArgs>(value_args)...))};
  if (!cbor_map_add(&map, std::move(kv))) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize ", key, " to CBOR"));
  }

  return absl::OkStatus();
}

absl::Status CborSerializeString(absl::string_view key, absl::string_view value,
                                 cbor_item_t& root) {
  return CborSerializeKeyValue(key, &cbor_build_stringn, root, value.data(),
                               value.size());
}

absl::Status CborSerializeBool(absl::string_view key, bool value,
                               cbor_item_t& root) {
  return CborSerializeKeyValue(key, &cbor_build_bool, root, value);
}

absl::Status CborSerializeByteString(absl::string_view key,
                                     absl::string_view value,
                                     cbor_item_t& root) {
  cbor_data data = reinterpret_cast<const unsigned char*>(value.data());
  return CborSerializeKeyValue(key, &cbor_build_bytestring, root, data,
                               value.size());
}

absl::Status CborSerializeStringArray(
    absl::string_view key, const RepeatedPtrField<std::string>& value,
    cbor_item_t& root) {
  ScopedCbor array_encoded(cbor_new_definite_array(value.size()));
  for (const auto& element : value) {
    cbor_item_t* to_push =
        cbor_move(cbor_build_stringn(element.data(), element.size()));
    if (!cbor_array_push(*array_encoded, to_push)) {
      return absl::InternalError(absl::StrCat("Unable to serialize: ", key));
    }
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *array_encoded};
  if (!cbor_map_add(&root, std::move(kv))) {
    return absl::InternalError(
        absl::StrCat("Failed to add serialized  ", key, " to overall CBOR"));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::vector<PrevWin>> DecodeJsonArrayPrevWins(
    absl::string_view prev_wins) {
  rapidjson::Document prev_wins_array;
  prev_wins_array.SetArray();
  rapidjson::ParseResult parse_result = prev_wins_array.Parse(prev_wins.data());
  if (parse_result.IsError()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Malformed prev wins, error: ",
                     rapidjson::GetParseError_En(parse_result.Code()),
                     " at offset: ", parse_result.Offset()));
  }

  std::vector<PrevWin> result;
  for (const auto& prev_win_json_value : prev_wins_array.GetArray()) {
    const auto& prev_win_pair = prev_win_json_value.GetArray();
    const auto& relative_time_val = prev_win_pair[kRelativeTimeIndex];
    const auto& ad_render_id_val = prev_win_pair[kAdRenderIdIndex];
    result.push_back(
        {relative_time_val.GetInt64(), ad_render_id_val.GetString()});
  }
  return result;
}

absl::Status CborSerializeBrowserSignals(absl::string_view key,
                                         const BrowserSignals& browser_signals,
                                         cbor_item_t& interest_group_root) {
  ScopedCbor browser_signals_map(cbor_new_definite_map(kNumBrowserSignalKeys));

  PS_RETURN_IF_ERROR(CborSerializeKeyValue(kJoinCount, &cbor_build_uint64,
                                           **browser_signals_map,
                                           browser_signals.join_count()));
  PS_RETURN_IF_ERROR(CborSerializeKeyValue(kBidCount, &cbor_build_uint64,
                                           **browser_signals_map,
                                           browser_signals.bid_count()));
  PS_RETURN_IF_ERROR(CborSerializeKeyValue(kRecency, &cbor_build_uint64,
                                           **browser_signals_map,
                                           browser_signals.recency()));
  std::vector<PrevWin> prev_wins_to_encode;
  if (!browser_signals.prev_wins().empty()) {
    PS_ASSIGN_OR_RETURN(prev_wins_to_encode,
                        DecodeJsonArrayPrevWins(browser_signals.prev_wins()));
    ScopedCbor wins(cbor_new_definite_array(prev_wins_to_encode.size()));
    for (const auto& [time_entry, ad_render_id] : prev_wins_to_encode) {
      ScopedCbor pair(cbor_new_definite_array(2));
      if (!cbor_array_push(*pair, cbor_move(cbor_build_uint64(time_entry)))) {
        return absl::InvalidArgumentError(
            "Unable to serialize time in prev wins");
      }

      if (!cbor_array_push(
              *pair, cbor_move(cbor_build_stringn(ad_render_id.data(),
                                                  ad_render_id.size())))) {
        return absl::InvalidArgumentError(
            "Unable to serialize ad render id in prev wins");
      }

      if (!cbor_array_push(*wins, *pair)) {
        return absl::InvalidArgumentError(
            "Unable to serialize time/ad render id pair in prev wins");
      }
    }
    struct cbor_pair kv = {
        .key = cbor_move(cbor_build_stringn(kPrevWins, sizeof(kPrevWins) - 1)),
        .value = *wins};
    if (!cbor_map_add(*browser_signals_map, std::move(kv))) {
      return absl::InvalidArgumentError(
          "Unable to serialize the complete prev wins data");
    }
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *browser_signals_map};
  if (!cbor_map_add(&interest_group_root, std::move(kv))) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize ", key, " to CBOR"));
  }

  return absl::OkStatus();
}

absl::Status CborSerializeAdRenderUrls(
    absl::string_view incoming_key,
    const google::protobuf::ListValue& ads_list_value, cbor_item_t& root) {
  ScopedCbor ads_encoded(cbor_new_definite_array(ads_list_value.values_size()));
  for (const auto& ad_value : ads_list_value.values()) {
    ScopedCbor ad_encoded(cbor_new_definite_map(kNumAdsFields));
    // Traversing keys in a sorted fashion to ensure encoded string is the same
    // everytime. This is important for testing.
    std::set<std::string> sorted_keys;
    for (const auto& [key, val] : ad_value.struct_value().fields()) {
      sorted_keys.insert(key);
    }
    for (const auto& key : sorted_keys) {
      const auto& val = ad_value.struct_value().fields().at(key);
      int index = FindKeyIndex(kAdsFields, key);
      switch (index) {
        case 0: {  // kMetadata
          ScopedCbor metadata_encoded(
              cbor_new_definite_array(val.list_value().values_size()));
          for (const auto& metadata_val : val.list_value().values()) {
            const auto& string_val = metadata_val.string_value();
            if (!cbor_array_push(*metadata_encoded,
                                 cbor_move(cbor_build_stringn(
                                     string_val.data(), string_val.size())))) {
              return absl::InvalidArgumentError(
                  "Unable to serialize one of metadata string value");
            }
          }
          struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                                     kMetadata, sizeof(kMetadata) - 1)),
                                 .value = *metadata_encoded};
          if (!cbor_map_add(*ad_encoded, std::move(kv))) {
            return absl::InternalError(
                absl::StrCat("Failed to serialize ", kMetadata, " to CBOR"));
          }
        } break;
        case 1: {  // kRenderUrl
          const std::string& string_val = val.string_value();
          PS_RETURN_IF_ERROR(CborSerializeKeyValue(
              kRenderUrl, &cbor_build_stringn, **ad_encoded, string_val.data(),
              string_val.size()));
        } break;
        default:
          // Ignore any unknown keys.
          VLOG(2) << "Found unexpected key in ads (under interest group): "
                  << key;
          continue;
      }
    }
    if (!cbor_array_push(*ads_encoded, *ad_encoded)) {
      return absl::InternalError(
          absl::StrCat("Failed to serialize one of the ads"));
    }
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             incoming_key.data(), incoming_key.size())),
                         .value = *ads_encoded};
  if (!cbor_map_add(&root, std::move(kv))) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize ", incoming_key, " to CBOR"));
  }

  return absl::OkStatus();
}

absl::Status CborSerializeInterestGroup(
    const BuyerInput::InterestGroup& interest_group, cbor_item_t& root) {
  cbor_item_t* interest_group_serialized =
      cbor_new_definite_map(kNumInterestGroupKeys);
  PS_RETURN_IF_ERROR(CborSerializeString(kName, interest_group.name(),
                                         *interest_group_serialized));
  PS_RETURN_IF_ERROR(CborSerializeStringArray(
      kBiddingSignalsKeys, interest_group.bidding_signals_keys(),
      *interest_group_serialized));
  PS_RETURN_IF_ERROR(CborSerializeString(kUserBiddingSignals,
                                         interest_group.user_bidding_signals(),
                                         *interest_group_serialized));
  PS_RETURN_IF_ERROR(CborSerializeAdRenderUrls(kAds, interest_group.ads(),
                                               *interest_group_serialized));
  PS_RETURN_IF_ERROR(CborSerializeStringArray(kAdComponents,
                                              interest_group.component_ads(),
                                              *interest_group_serialized));
  PS_RETURN_IF_ERROR(CborSerializeBrowserSignals(
      kBrowserSignals, interest_group.browser_signals(),
      *interest_group_serialized));

  if (!cbor_array_push(&root, cbor_move(interest_group_serialized))) {
    return absl::InternalError(
        "Failed to add interest group to serialized CBOR");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeBuyerInput(const BuyerInputMapEncoded& buyer_inputs,
                                     cbor_item_t& root) {
  cbor_item_t* serialized_buyer_input =
      cbor_new_definite_map(buyer_inputs.size());
  BuyerInputMapEncoded encoded_buyer_input;
  for (const auto& [owner, encoded_buyer_input] : buyer_inputs) {
    PS_RETURN_IF_ERROR(CborSerializeByteString(owner, encoded_buyer_input,
                                               *serialized_buyer_input));
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kInterestGroups, sizeof(kInterestGroups) - 1)),
                         .value = cbor_move(serialized_buyer_input)};
  if (!cbor_map_add(&root, std::move(kv))) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize ", kInterestGroups, " to CBOR"));
  }

  return absl::OkStatus();
}

std::string CborDecodeString(cbor_item_t* input) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(input)),
                     cbor_string_length(input));
}

google::protobuf::RepeatedPtrField<std::string> CborDecodeComponentAdUrls(
    cbor_item_t* input) {
  google::protobuf::RepeatedPtrField<std::string> component_ad_urls;
  absl::Span<cbor_item_t*> component_ad_url_entries(cbor_array_handle(input),
                                                    cbor_array_size(input));
  for (cbor_item_t* entry : component_ad_url_entries) {
    *component_ad_urls.Add() = CborDecodeString(entry);
  }
  return component_ad_urls;
}

absl::StatusOr<AuctionResult::Error> CborDecodeErrorToProto(
    cbor_item_t* serialized_error) {
  AuctionResult::Error error;
  absl::Span<struct cbor_pair> error_entries(cbor_map_handle(serialized_error),
                                             cbor_map_size(serialized_error));
  for (const auto& kv : error_entries) {
    if (!cbor_isa_string(kv.key)) {
      return absl::InvalidArgumentError("Expect error keys to be a string");
    }
    std::string key = CborDecodeString(kv.key);
    switch (FindKeyIndex<kNumErrorKeys>(kErrorKeys, key)) {
      case 0:  // kMessage
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected error message to be a string");
        }
        error.set_message(CborDecodeString(kv.value));
        break;
      case 1:  // kCode
        if (!cbor_is_int(kv.value)) {
          return absl::InvalidArgumentError("Expected error code to be an int");
        }
        error.set_code(cbor_get_int(kv.value));
        break;
      default:
        return absl::InvalidArgumentError(
            absl::StrCat("Unexpected key found in error: ", key));
    }
  }
  return error;
}

absl::StatusOr<BiddingGroupMap> CborDecodeInterestGroupToProto(
    cbor_item_t* serialized_groups) {
  BiddingGroupMap bidding_group_map;
  absl::Span<struct cbor_pair> group_entries(cbor_map_handle(serialized_groups),
                                             cbor_map_size(serialized_groups));
  for (const auto& kv : group_entries) {
    if (!cbor_isa_string(kv.key) || !cbor_isa_array(kv.value)) {
      return absl::InvalidArgumentError(
          "Malformed Interest group, either key is not string or value is not "
          "an array");
    }

    absl::Span<cbor_item_t*> index_entries(cbor_array_handle(kv.value),
                                           cbor_array_size(kv.value));
    AuctionResult::InterestGroupIndex interest_group_index;
    for (const auto& index_entry : index_entries) {
      if (!cbor_is_int(index_entry)) {
        return absl::InvalidArgumentError("Interest group index is not an int");
      }
      interest_group_index.add_index(cbor_get_int(index_entry));
    }
    bidding_group_map.emplace(CborDecodeString(kv.key),
                              std::move(interest_group_index));
  }
  return bidding_group_map;
}

}  // namespace

absl::StatusOr<AuctionResult> CborDecodeAuctionResultToProto(
    const std::string& serialized_input) {
  AuctionResult auction_result;
  cbor_load_result cbor_result;
  std::vector<unsigned char> bytes(serialized_input.begin(),
                                   serialized_input.end());
  cbor_item_t* loaded_data =
      cbor_load(bytes.data(), bytes.size(), &cbor_result);

  // General pattern here is to check that error code is not equal to
  // CBOR_ERR_NONE but that consistently fails even for simple encode/decode
  // examples.
  if (loaded_data == nullptr) {
    return absl::InternalError(
        "Failed to load CBOR encoded auction result data");
  }

  ScopedCbor root(loaded_data);
  if (!cbor_isa_map(root.get())) {
    return absl::InternalError(
        "Expected CBOR encoded auction result to be a map");
  }

  absl::Span<struct cbor_pair> auction_result_entries(
      cbor_map_handle(root.get()), cbor_map_size(root.get()));
  for (const auto& kv : auction_result_entries) {
    std::string key = CborDecodeString(kv.key);
    switch (FindKeyIndex(kAuctionResultKeys, key)) {
      case 0:  // kScore
        if (!cbor_isa_float_ctrl(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected score value to be a float");
        }
        auction_result.set_score(cbor_float_get_float(kv.value));
        break;
      case 1:  // kBid
        if (!cbor_isa_float_ctrl(kv.value)) {
          return absl::InvalidArgumentError("Expected bid value to be a float");
        }
        auction_result.set_bid(cbor_float_get_float(kv.value));
        break;
      case 2:  // kChaff
        if (!cbor_is_bool(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected is_chaff value to be a bool");
        }
        auction_result.set_is_chaff(cbor_get_bool(kv.value));
        break;
      case 3:  // kAdRenderUrl
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected render_url value to be a string");
        }
        auction_result.set_ad_render_url(CborDecodeString(kv.value));
        break;
      case 4: {  // kBiddingGroups
        if (!cbor_isa_map(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected bidding_groups value to be a map");
        }
        absl::StatusOr<BiddingGroupMap> bidding_groups;
        PS_ASSIGN_OR_RETURN(bidding_groups,
                            CborDecodeInterestGroupToProto(kv.value));
        *auction_result.mutable_bidding_groups() = std::move(*bidding_groups);
        break;
      }
      case 5:  // kInterestGroupName
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected interest group name value to be a string");
        }
        auction_result.set_interest_group_name(CborDecodeString(kv.value));
        break;
      case 6:  // kInterestGroupOwner
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected interest group owner value to be a string");
        }
        auction_result.set_interest_group_owner(CborDecodeString(kv.value));
        break;
      case 7:  // kAdComponents
        if (!cbor_isa_array(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected component ad URLs to be an array");
        }
        *auction_result.mutable_ad_component_render_urls() =
            CborDecodeComponentAdUrls(kv.value);
        break;
      case 8: {  // kError
        if (!cbor_isa_map(kv.value)) {
          return absl::InvalidArgumentError("Expected error value to be a map");
        }
        AuctionResult::Error error;
        PS_ASSIGN_OR_RETURN(error, CborDecodeErrorToProto(kv.value));
        *auction_result.mutable_error() = std::move(error);
      } break;
      default:
        // Unexpected key in the auction result CBOR
        return absl::Status(
            absl::StatusCode::kInvalidArgument,
            absl::StrCat(
                "Serialized CBOR auction result has an unknown root key: ",
                key));
    }
  }
  return auction_result;
}

absl::StatusOr<std::string> CborEncodeProtectedAudienceProto(
    const ProtectedAudienceInput& protected_audience_input) {
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumRequestRootKeys));
  auto* cbor_internal = cbor_data_root.get();

  PS_RETURN_IF_ERROR(CborSerializeString(
      kGenerationId, protected_audience_input.generation_id(), *cbor_internal));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kPublisher, protected_audience_input.publisher_name(), *cbor_internal));
  PS_RETURN_IF_ERROR(CborSerializeBool(
      kDebugReporting, protected_audience_input.enable_debug_reporting(),
      *cbor_internal));
  PS_RETURN_IF_ERROR(CborSerializeBuyerInput(
      protected_audience_input.buyer_input(), *cbor_internal));
  return SerializeCbor(*cbor_data_root);
}

absl::StatusOr<BuyerInputMapEncoded> GetEncodedBuyerInputMap(
    const BuyerInputMap& buyer_inputs) {
  BuyerInputMapEncoded encoded_buyer_input;
  for (const auto& [owner, buyer_input] : buyer_inputs) {
    // Serialize the list of interest groups.
    ScopedCbor scoped_interest_groups_list(
        cbor_new_definite_array(buyer_input.interest_groups_size()));
    cbor_item_t* interest_groups_list = *scoped_interest_groups_list;
    for (const auto& interest_group : buyer_input.interest_groups()) {
      PS_RETURN_IF_ERROR(
          CborSerializeInterestGroup(interest_group, *interest_groups_list));
    }
    std::string serialized_interest_groups =
        SerializeCbor(interest_groups_list);
    absl::StatusOr<std::string> compressed_data;
    PS_ASSIGN_OR_RETURN(compressed_data,
                        GzipCompress(std::move(serialized_interest_groups)));
    encoded_buyer_input.emplace(owner, std::move(*compressed_data));
  }
  return encoded_buyer_input;
}

std::string SerializeCbor(cbor_item_t* root) {
  const size_t kSerialzedDataSize = cbor_serialized_size(root);
  char buffer[kSerialzedDataSize];
  size_t actual_cbor_size = cbor_serialize(
      root, reinterpret_cast<unsigned char*>(buffer), kSerialzedDataSize);
  return std::string(buffer, actual_cbor_size);
}

}  // namespace privacy_sandbox::bidding_auction_servers
