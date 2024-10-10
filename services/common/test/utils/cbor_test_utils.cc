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

#include <string>
#include <utility>
#include <vector>

#include <rapidjson/error/en.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "cbor/strings.h"
#include "rapidjson/document.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/util/status_macro/status_macros.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::protobuf::RepeatedPtrField;
using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using BuyerInputMap = ::google::protobuf::Map<std::string, BuyerInput>;
using BuyerInputMapEncoded = ::google::protobuf::Map<std::string, std::string>;
using InteractionUrlMap = ::google::protobuf::Map<std::string, std::string>;

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
  if (!cbor_map_add(&map, kv)) {
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
  if (!cbor_map_add(&root, kv)) {
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
  if (browser_signals.recency()) {
    PS_RETURN_IF_ERROR(CborSerializeKeyValue(kRecency, &cbor_build_uint64,
                                             **browser_signals_map,
                                             browser_signals.recency()));
  }
  if (browser_signals.recency_ms()) {
    PS_RETURN_IF_ERROR(CborSerializeKeyValue(kRecencyMs, &cbor_build_uint64,
                                             **browser_signals_map,
                                             browser_signals.recency_ms()));
  }
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
    if (!cbor_map_add(*browser_signals_map, kv)) {
      return absl::InvalidArgumentError(
          "Unable to serialize the complete prev wins data");
    }
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *browser_signals_map};
  if (!cbor_map_add(&interest_group_root, kv)) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize ", key, " to CBOR"));
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
  PS_RETURN_IF_ERROR(CborSerializeStringArray(kAdComponents,
                                              interest_group.component_ads(),
                                              *interest_group_serialized));
  PS_RETURN_IF_ERROR(CborSerializeStringArray(
      kAds, interest_group.ad_render_ids(), *interest_group_serialized));
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
  if (!cbor_map_add(&root, kv)) {
    return absl::InternalError(
        absl::StrCat("Failed to serialize ", kInterestGroups, " to CBOR"));
  }

  return absl::OkStatus();
}

absl::Status CborSerializeConsentedDebugConfig(
    const server_common::ConsentedDebugConfiguration& consented_debug_config,
    cbor_item_t& root) {
  ScopedCbor serialized_consented_debug_config(
      cbor_new_definite_map(kNumConsentedDebugConfigKeys));
  PS_RETURN_IF_ERROR(CborSerializeBool(kIsConsented,
                                       consented_debug_config.is_consented(),
                                       **serialized_consented_debug_config));
  if (!consented_debug_config.token().empty()) {
    PS_RETURN_IF_ERROR(
        CborSerializeString(kToken, consented_debug_config.token(),
                            **serialized_consented_debug_config));
  }
  PS_RETURN_IF_ERROR(CborSerializeBool(
      kIsDebugResponse, consented_debug_config.is_debug_info_in_response(),
      **serialized_consented_debug_config));
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kConsentedDebugConfig,
                                          sizeof(kConsentedDebugConfig) - 1)),
      .value = *serialized_consented_debug_config,
  };
  if (!cbor_map_add(&root, kv)) {
    return absl::InternalError(absl::StrCat("Failed to serialize ",
                                            kConsentedDebugConfig, " to CBOR"));
  }
  return absl::OkStatus();
}

}  // namespace

template <typename T>
absl::StatusOr<std::string> CborEncodeProtectedAuctionProtoHelper(
    const T& protected_auction_input) {
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumRequestRootKeys));
  auto* cbor_internal = cbor_data_root.get();

  PS_RETURN_IF_ERROR(CborSerializeString(
      kGenerationId, protected_auction_input.generation_id(), *cbor_internal));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kPublisher, protected_auction_input.publisher_name(), *cbor_internal));
  PS_RETURN_IF_ERROR(CborSerializeBool(
      kDebugReporting, protected_auction_input.enable_debug_reporting(),
      *cbor_internal));
  PS_RETURN_IF_ERROR(CborSerializeBuyerInput(
      protected_auction_input.buyer_input(), *cbor_internal));
  if (protected_auction_input.has_consented_debug_config()) {
    PS_RETURN_IF_ERROR(CborSerializeConsentedDebugConfig(
        protected_auction_input.consented_debug_config(), *cbor_internal));
  }
  PS_RETURN_IF_ERROR(CborSerializeBool(
      kEnforceKAnon, protected_auction_input.enforce_kanon(), *cbor_internal));
  return SerializeCbor(*cbor_data_root);
}

absl::StatusOr<std::string> CborEncodeProtectedAuctionProto(
    const ProtectedAudienceInput& protected_auction_input) {
  return CborEncodeProtectedAuctionProtoHelper(protected_auction_input);
}

absl::StatusOr<std::string> CborEncodeProtectedAuctionProto(
    const ProtectedAuctionInput& protected_auction_input) {
  return CborEncodeProtectedAuctionProtoHelper(protected_auction_input);
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
