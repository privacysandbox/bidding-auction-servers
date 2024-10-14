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
#include <set>
#include <string>
#include <utility>

#include "absl/numeric/bits.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "services/common/compression/gzip.h"
#include "src/util/status_macro/status_macros.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using InteractionUrlMap = ::google::protobuf::Map<std::string, std::string>;
using ErrorHandler = const std::function<void(const grpc::Status&)>&;
using RepeatedStringProto = ::google::protobuf::RepeatedPtrField<std::string>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using GhostWinnerForTopLevelAuction =
    AuctionResult::KAnonGhostWinner::GhostWinnerForTopLevelAuction;
using GhostWinnerPrivateAggregationSignals =
    AuctionResult::KAnonGhostWinner::GhostWinnerPrivateAggregationSignals;

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

struct cbor_pair BuildCborKVPair(absl::string_view key,
                                 absl::string_view value) {
  return {.key = cbor_move(cbor_build_stringn(key.data(), key.size())),
          .value = cbor_move(cbor_build_stringn(value.data(), value.size()))};
}

absl::Status AddKVToMap(absl::string_view key, absl::string_view value,
                        ErrorHandler error_handler, cbor_item_t& map) {
  if (!cbor_map_add(&map, BuildCborKVPair(key, value))) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
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
      case 4: {  // RecencyMs.
        bool is_recency_valid_type =
            IsTypeValid(&cbor_is_int, signal.value, kBrowserSignalsRecencyMs,
                        kInt, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, signals);
        if (is_recency_valid_type) {
          signals.set_recency_ms(cbor_get_int(signal.value));
        }
        break;
      }
    }
  }

  return signals;
}

absl::Status CborSerializeString(absl::string_view key, absl::string_view value,
                                 ErrorHandler error_handler,
                                 cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_stringn(value.data(), value.size()))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeFloat(absl::string_view key, double value,
                                ErrorHandler error_handler, cbor_item_t& root) {
  PS_ASSIGN_OR_RETURN(cbor_item_t * float_val, cbor_build_float(value));
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(float_val)};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeBool(absl::string_view key, bool value,
                               ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_bool(value))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeInt(absl::string_view key, int value,
                              ErrorHandler error_handler, cbor_item_t& root) {
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = cbor_move(cbor_build_int(value))};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
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
      error_handler(
          grpc::Status(grpc::INTERNAL,
                       absl::StrCat("Failed to serialize ", key, " to CBOR")));
      return absl::InternalError("");
    }
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_component_renders};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializekAnonJoinCandidates(
    absl::string_view key,
    const AuctionResult::KAnonJoinCandidate& kanon_join_candidate,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_kanon_join_candidates(
      cbor_new_definite_map(kNumKAnonJoinCandidateKeys));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kAdRenderUrlHash, kanon_join_candidate.ad_render_url_hash(),
      error_handler, **serialized_kanon_join_candidates));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kReportingIdHash, kanon_join_candidate.reporting_id_hash(), error_handler,
      **serialized_kanon_join_candidates));
  const auto& input_ad_component_render_urls_hash =
      kanon_join_candidate.ad_component_render_urls_hash();
  ScopedCbor ad_component_render_urls_hash(
      cbor_new_definite_array(input_ad_component_render_urls_hash.size()));
  for (const auto& ad_component_render_url_hash :
       input_ad_component_render_urls_hash) {
    if (!cbor_array_push(*ad_component_render_urls_hash,
                         cbor_move(cbor_build_stringn(
                             ad_component_render_url_hash.data(),
                             ad_component_render_url_hash.size())))) {
      error_handler(grpc::Status(
          grpc::INTERNAL,
          "Failed to serialize ad component render URL hash to CBOR"));
      return absl::InternalError("");
    }
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(
          kAdComponentRenderUrlsHash, sizeof(kAdComponentRenderUrlsHash) - 1)),
      .value = *ad_component_render_urls_hash};
  if (!cbor_map_add(*serialized_kanon_join_candidates, kv)) {
    error_handler(
        grpc::Status(grpc::INTERNAL,
                     "Failed to serialize kAdComponentRenderUrlsHash to CBOR"));
    return absl::InternalError("");
  }
  struct cbor_pair outer_kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_kanon_join_candidates};
  if (!cbor_map_add(&root, outer_kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, "Failed to serialize kAnonJoinCandidate to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializekAnonGhostWinnerForTopLevelAuction(
    absl::string_view key,
    const GhostWinnerForTopLevelAuction& ghost_winner_for_top_level_auction,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_ghost_winner(
      cbor_new_definite_map(kNumGhostWinnerForTopLevelAuctionKeys));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kAdMetadata, ghost_winner_for_top_level_auction.ad_metadata(),
      error_handler, **serialized_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kAdRenderUrl, ghost_winner_for_top_level_auction.ad_render_url(),
      error_handler, **serialized_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kBidCurrency, ghost_winner_for_top_level_auction.bid_currency(),
      error_handler, **serialized_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializeFloat(
      kModifiedBid, ghost_winner_for_top_level_auction.modified_bid(),
      error_handler, **serialized_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kBuyerReportingId,
      ghost_winner_for_top_level_auction.buyer_reporting_id(), error_handler,
      **serialized_ghost_winner));

  ScopedCbor serialized_ad_component_render_urls(cbor_new_definite_array(
      ghost_winner_for_top_level_auction.ad_component_render_urls_size()));
  for (const auto& ad_component_render_url :
       ghost_winner_for_top_level_auction.ad_component_render_urls()) {
    if (!cbor_array_push(
            *serialized_ad_component_render_urls,
            cbor_move(cbor_build_stringn(ad_component_render_url.data(),
                                         ad_component_render_url.size())))) {
      error_handler(grpc::Status(grpc::INTERNAL,
                                 "Failed to serialize a ad component render "
                                 "url for ghost winner to CBOR"));
      return absl::InternalError("");
    }
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kAdComponentRenderUrls,
                                          sizeof(kAdComponentRenderUrls) - 1)),
      .value = *serialized_ad_component_render_urls};
  if (!cbor_map_add(*serialized_ghost_winner, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, "Failed to serialize adComponentRenderUrls to CBOR"));
    return absl::InternalError("");
  }
  struct cbor_pair outer_kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_ghost_winner};
  if (!cbor_map_add(&root, outer_kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, "Failed to serialize kAnonJoinCandidate to CBOR"));
    return absl::InternalError("");
  }
  PS_RETURN_IF_ERROR(CborSerializeString(
      kBuyerAndSellerReportingId,
      ghost_winner_for_top_level_auction.buyer_and_seller_reporting_id(),
      error_handler, **serialized_ghost_winner));
  PS_RETURN_IF_ERROR(
      CborSerializeString(kSelectableBuyerAndSellerReportingId,
                          ghost_winner_for_top_level_auction
                              .selectable_buyer_and_seller_reporting_id(),
                          error_handler, **serialized_ghost_winner));
  return absl::OkStatus();
}

absl::Status CborSerializekAnonGhostWinnerPrivateAggSignals(
    absl::string_view key,
    const GhostWinnerPrivateAggregationSignals& private_agg_signals,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_private_agg_signals(
      cbor_new_definite_map(kNumGhostWinnerPrivateAggregationSignalsKeys));
  PS_RETURN_IF_ERROR(CborSerializeInt(kValue, private_agg_signals.value(),
                                      error_handler,
                                      **serialized_private_agg_signals));
  PS_RETURN_IF_ERROR(CborSerializeString(kBucket, private_agg_signals.bucket(),
                                         error_handler,
                                         **serialized_private_agg_signals));
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_private_agg_signals};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, "Failed to serialize kAnonJoinCandidate to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializeKAnonGhostWinner(
    const AuctionResult::KAnonGhostWinner& kanon_ghost_winner,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_kanon_ghost_winner(
      cbor_new_definite_map(kNumKAnonGhostWinnerKeys));
  PS_RETURN_IF_ERROR(CborSerializeString(kOwner, kanon_ghost_winner.owner(),
                                         error_handler,
                                         **serialized_kanon_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializeInt(
      kInterestGroupIndex, kanon_ghost_winner.interest_group_index(),
      error_handler, **serialized_kanon_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializekAnonJoinCandidates(
      kKAnonJoinCandidates, kanon_ghost_winner.k_anon_join_candidates(),
      error_handler, **serialized_kanon_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializekAnonGhostWinnerForTopLevelAuction(
      kGhostWinnerForTopLevelAuction,
      kanon_ghost_winner.ghost_winner_for_top_level_auction(), error_handler,
      **serialized_kanon_ghost_winner));
  PS_RETURN_IF_ERROR(CborSerializekAnonGhostWinnerPrivateAggSignals(
      kGhostWinnerPrivateAggregationSignals,
      kanon_ghost_winner.ghost_winner_private_aggregation_signals(),
      error_handler, **serialized_kanon_ghost_winner));
  if (!cbor_array_push(&root, *serialized_kanon_ghost_winner)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        "Failed to serialize ad component render URL hash to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializeKAnonGhostWinners(
    absl::string_view key,
    const std::vector<AuctionResult::KAnonGhostWinner>& kanon_ghost_winners,
    ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_kanon_ghost_winners(
      cbor_new_definite_array(kanon_ghost_winners.size()));
  for (const auto& kanon_ghost_winner : kanon_ghost_winners) {
    if (auto status =
            CborSerializeKAnonGhostWinner(kanon_ghost_winner, error_handler,
                                          **serialized_kanon_ghost_winners);
        !status.ok()) {
      error_handler(grpc::Status(
          grpc::INTERNAL, "Failed to serialize a kAnonGhostWinner to CBOR"));
      return absl::InternalError("");
    }
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kKAnonGhostWinners,
                                          sizeof(kKAnonGhostWinners) - 1)),
      .value = *serialized_kanon_ghost_winners};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, "Failed to serialize kAnonGhostWinners to CBOR"));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializeScoreAdResponse(
    const ScoreAdsResponse::AdScore& ad_score,
    const BiddingGroupMap& bidding_group_map,
    const UpdateGroupMap& update_group_map,
    const KAnonAuctionResultData* kanon_auction_result_data,
    ErrorHandler error_handler, cbor_item_t& root) {
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kBid, ad_score.buyer_bid(), error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kScore, ad_score.desirability(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeBool(kChaff, false, error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeAdComponentUrls(
      kAdComponents, ad_score.component_renders(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kAdRenderUrl, ad_score.render(),
                                         error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeUpdateGroups(update_group_map, error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeBiddingGroups(bidding_group_map, error_handler, root));
  if (!ad_score.buyer_reporting_id().empty()) {
    PS_RETURN_IF_ERROR(CborSerializeString(
        kBuyerReportingId, ad_score.buyer_reporting_id(), error_handler, root));
  }
  PS_RETURN_IF_ERROR(CborSerializeWinReportingUrls(
      ad_score.win_reporting_urls(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kInterestGroupName, ad_score.interest_group_name(), error_handler, root));
  if (kanon_auction_result_data != nullptr) {
    PS_RETURN_IF_ERROR(CborSerializeKAnonGhostWinners(
        kKAnonGhostWinners, kanon_auction_result_data->kanon_ghost_winners,
        error_handler, root));
  }
  PS_RETURN_IF_ERROR(CborSerializeString(kInterestGroupOwner,
                                         ad_score.interest_group_owner(),
                                         error_handler, root));
  if (kanon_auction_result_data != nullptr) {
    PS_RETURN_IF_ERROR(CborSerializekAnonJoinCandidates(
        kKAnonWinnerJoinCandidates,
        kanon_auction_result_data->kanon_winner_join_candidates, error_handler,
        root));
    PS_RETURN_IF_ERROR(CborSerializeInt(
        kKAnonWinnerPositionalIndex,
        kanon_auction_result_data->kanon_winner_positional_index, error_handler,
        root));
  }
  return absl::OkStatus();
}

absl::Status CborSerializeComponentScoreAdResponse(
    absl::string_view top_level_seller,
    const ScoreAdsResponse::AdScore& ad_score,
    const BiddingGroupMap& bidding_group_map,
    const UpdateGroupMap& update_group_map, ErrorHandler error_handler,
    cbor_item_t& root) {
  // Logic in the rest of the system guarantees that:
  // - buyer_bid must be > 0 for the AdWithBid to be scored
  // - modified bid is replaced by buyer_bid if modified bid is <= 0
  // Therefore if modified bid is 0 here,
  // there must have been an error in B&A logic.
  // Chrome regards modified bids of 0 as invalid and will reject them.
  // Thus we return an error for modified bids <= 0.
  if (ad_score.bid() <= 0.0f) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Modified bid should never be zero, logic error");
  }
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kBid, ad_score.bid(), error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeFloat(kScore, ad_score.desirability(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeBool(kChaff, false, error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kAdMetadata, ad_score.ad_metadata(),
                                         error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeAdComponentUrls(
      kAdComponents, ad_score.component_renders(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kAdRenderUrl, ad_score.render(),
                                         error_handler, root));
  if (!ad_score.bid_currency().empty()) {
    PS_RETURN_IF_ERROR(CborSerializeString(
        kBidCurrency, ad_score.bid_currency(), error_handler, root));
  }
  PS_RETURN_IF_ERROR(
      CborSerializeUpdateGroups(update_group_map, error_handler, root));
  PS_RETURN_IF_ERROR(
      CborSerializeBiddingGroups(bidding_group_map, error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kTopLevelSeller, top_level_seller,
                                         error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeWinReportingUrls(
      ad_score.win_reporting_urls(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(
      kInterestGroupName, ad_score.interest_group_name(), error_handler, root));
  PS_RETURN_IF_ERROR(CborSerializeString(kInterestGroupOwner,
                                         ad_score.interest_group_owner(),
                                         error_handler, root));
  return absl::OkStatus();
}

absl::StatusOr<std::string> GetCborSerializedAuctionResult(
    ErrorHandler error_handler, cbor_item_t& cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size =
      cbor_serialized_size(&cbor_data_root);
  if (!cbor_serialized_data_size) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        "Failed to serialize the AuctionResult to CBOR (data is too large!)"));
    return absl::InternalError("");
  }

  std::string byte_string;
  byte_string.resize(cbor_serialized_data_size);
  if (cbor_serialize(&cbor_data_root,
                     reinterpret_cast<unsigned char*>(byte_string.data()),
                     cbor_serialized_data_size) == 0) {
    error_handler(grpc::Status(
        grpc::INTERNAL, "Failed to serialize the AuctionResult to CBOR"));
    return absl::InternalError("");
  }
  return byte_string;
}

absl::Status CborSerializeError(const AuctionResult::Error& error,
                                ErrorHandler error_handler, cbor_item_t& root) {
  ScopedCbor serialized_error_map(cbor_new_definite_map(kNumErrorKeys));
  struct cbor_pair code_kv = {
      .key = cbor_move(cbor_build_stringn(kCode, sizeof(kCode) - 1)),
      .value = cbor_move(cbor_build_uint(error.code()))};
  if (!cbor_map_add(*serialized_error_map, code_kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize error ", kCode, " to CBOR")));
    return absl::InternalError("");
  }

  const std::string& message = error.message();
  struct cbor_pair message_kv = {
      .key = cbor_move(cbor_build_stringn(kMessage, sizeof(kMessage) - 1)),
      .value = cbor_move(cbor_build_stringn(message.data(), message.size()))};
  if (!cbor_map_add(*serialized_error_map, message_kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize error ", kMessage, " to CBOR")));
    return absl::InternalError("");
  }

  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(kError, sizeof(kError) - 1)),
      .value = *serialized_error_map};
  if (!cbor_map_add(&root, kv)) {
    error_handler(
        grpc::Status(grpc::INTERNAL,
                     absl::StrCat("Failed to serialize ", kError, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::StatusOr<UpdateGroupMap> CborDecodeUpdateGroupMapToProto(
    cbor_item_t* serialized_groups) {
  UpdateGroupMap update_group_map;
  absl::Span<struct cbor_pair> owner_entries(cbor_map_handle(serialized_groups),
                                             cbor_map_size(serialized_groups));
  for (const auto& owner_entry : owner_entries) {
    if (!cbor_isa_string(owner_entry.key) ||
        !cbor_isa_array(owner_entry.value)) {
      return absl::InvalidArgumentError(
          "Malformed interest group update list, either key is not string or "
          "value is not an array");
    }
    absl::Span<cbor_item_t*> update_list_entries(
        cbor_array_handle(owner_entry.value),
        cbor_array_size(owner_entry.value));
    UpdateInterestGroupList update_list;

    for (const auto& update_entry : update_list_entries) {
      if (!cbor_isa_map(update_entry)) {
        return absl::InvalidArgumentError("Update interest group is not a map");
      }
      UpdateInterestGroup update;
      absl::Span<struct cbor_pair> interest_group(cbor_map_handle(update_entry),
                                                  cbor_map_size(update_entry));
      for (const auto& kv : interest_group) {
        std::string field = CborDecodeString(kv.key);
        if (!cbor_isa_string(kv.key)) {
          return absl::InvalidArgumentError(
              "Interest group update keys must be strings.");
        } else if (field == kIndex) {
          update.set_index(cbor_get_int(kv.value));
        } else if (field == kUpdateIfOlderThanMs) {
          update.set_update_if_older_than_ms(cbor_get_int(kv.value));
        } else {
          return absl::InvalidArgumentError(
              "Interest group update key invalid.");
        }
      }
      *update_list.mutable_interest_groups()->Add() = std::move(update);
    }

    update_group_map.emplace(CborDecodeString(owner_entry.key),
                             std::move(update_list));
  }
  return update_group_map;
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

google::protobuf::RepeatedPtrField<std::string> CborDecodeStringsArrToProto(
    cbor_item_t* input) {
  google::protobuf::RepeatedPtrField<std::string> string_arr;
  absl::Span<cbor_item_t*> string_arr_entries(cbor_array_handle(input),
                                              cbor_array_size(input));
  for (cbor_item_t* entry : string_arr_entries) {
    *string_arr.Add() = CborDecodeString(entry);
  }
  return string_arr;
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

absl::StatusOr<InteractionUrlMap> CborDecodeInteractionReportingUrlMapToProto(
    cbor_item_t* serialized_interaction_urls_map) {
  InteractionUrlMap interaction_url_map;
  absl::Span<struct cbor_pair> interaction_urls(
      cbor_map_handle(serialized_interaction_urls_map),
      cbor_map_size(serialized_interaction_urls_map));
  for (const auto& kv : interaction_urls) {
    if (!cbor_isa_string(kv.key) || !cbor_isa_string(kv.value)) {
      return absl::InvalidArgumentError("Malformed Interaction Reporting Urls");
    }
    interaction_url_map.try_emplace(CborDecodeString(kv.key),
                                    CborDecodeString(kv.value));
  }
  return interaction_url_map;
}

absl::Status CborDecodeReportingUrls(cbor_item_t* serialized_reporting_map,
                                     const std::string& outer_key,
                                     AuctionResult& auction_result) {
  absl::Span<struct cbor_pair> inner_map(
      cbor_map_handle(serialized_reporting_map),
      cbor_map_size(serialized_reporting_map));
  for (const auto& inner_kv : inner_map) {
    if (!cbor_isa_string(inner_kv.key)) {
      return absl::InvalidArgumentError(
          "Malformed inner map for winReportingUrls");
    }
    std::string reporting_url_key = CborDecodeString(inner_kv.key);
    switch (FindKeyIndex<kNumReportingUrlsKeys>(kReportingKeys,
                                                reporting_url_key)) {
      // kReportingUrl
      case 0: {
        if (!cbor_isa_string(inner_kv.value)) {
          return absl::InvalidArgumentError("Malformed Reporting Url value");
        }
        std::string reporting_url_value = CborDecodeString(inner_kv.value);
        switch (FindKeyIndex<kNumWinReportingUrlsKeys>(kWinReportingKeys,
                                                       outer_key)) {
          // kBuyerReportingUrls
          case 0:
            auction_result.mutable_win_reporting_urls()
                ->mutable_buyer_reporting_urls()
                ->set_reporting_url(reporting_url_value);
            break;
          // kComponentSellerReportingUrls
          case 1:
            auction_result.mutable_win_reporting_urls()
                ->mutable_component_seller_reporting_urls()
                ->set_reporting_url(reporting_url_value);
            break;
          // kTopLevelSellerReportingUrls
          case 2:
            auction_result.mutable_win_reporting_urls()
                ->mutable_top_level_seller_reporting_urls()
                ->set_reporting_url(reporting_url_value);
            break;
        }
      } break;
      // kInteractionReportingUrls
      case 1: {
        absl::StatusOr<InteractionUrlMap> interaction_url_map =
            CborDecodeInteractionReportingUrlMapToProto(inner_kv.value);
        if (!interaction_url_map.ok()) {
          return absl::InvalidArgumentError(
              "Error decoding interaction reporting urls for the buyer");
        }
        for (const auto& [event, url] : interaction_url_map.value()) {
          switch (FindKeyIndex<kNumWinReportingUrlsKeys>(kWinReportingKeys,
                                                         outer_key)) {
            case 0:  // winReportingURLs
              auction_result.mutable_win_reporting_urls()
                  ->mutable_buyer_reporting_urls()
                  ->mutable_interaction_reporting_urls()
                  ->try_emplace(event, url);
              break;
            case 1:  // componentSellerReportingURLs
              auction_result.mutable_win_reporting_urls()
                  ->mutable_component_seller_reporting_urls()
                  ->mutable_interaction_reporting_urls()
                  ->try_emplace(event, url);
              break;
            case 2:  // topLevelSellerReportingURLs
              auction_result.mutable_win_reporting_urls()
                  ->mutable_top_level_seller_reporting_urls()
                  ->mutable_interaction_reporting_urls()
                  ->try_emplace(event, url);
              break;
          }
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status CborDecodeReportingUrlsToProto(
    cbor_item_t* serialized_reporting_map, AuctionResult& auction_result) {
  absl::Span<struct cbor_pair> reporting_urls(
      cbor_map_handle(serialized_reporting_map),
      cbor_map_size(serialized_reporting_map));
  for (const auto& kv : reporting_urls) {
    if (!cbor_isa_string(kv.key)) {
      return absl::InvalidArgumentError("Malformed Reporting Urls");
    }
    std::string reporting_urls_key = CborDecodeString(kv.key);
    absl::Status decoding_status =
        CborDecodeReportingUrls(kv.value, reporting_urls_key, auction_result);
    if (!decoding_status.ok()) {
      return decoding_status;
    }
  }
  return absl::OkStatus();
}

absl::Status CborDecodeKAnonJoinCandidatesToProto(
    cbor_item_t* serialized_candidates_map,
    AuctionResult::KAnonJoinCandidate& kanon_join_candidate) {
  absl::Span<struct cbor_pair> winner_join_candidates(
      cbor_map_handle(serialized_candidates_map),
      cbor_map_size(serialized_candidates_map));
  bool any_key_found = false;
  for (const auto& kv : winner_join_candidates) {
    if (!cbor_isa_string(kv.key)) {
      return absl::InvalidArgumentError(
          "Malformed key type in kAnonWinnerJoinCandidates");
    }
    std::string key = CborDecodeString(kv.key);
    switch (FindKeyIndex<kNumKAnonJoinCandidateKeys>(kKAnonJoinCandidateKeys,
                                                     key)) {
      case 0:  // kAdRenderUrlHash
        kanon_join_candidate.set_ad_render_url_hash(CborDecodeString(kv.value));
        any_key_found = true;
        break;
      case 1:  // kAdComponentRenderUrlsHash
        if (!cbor_isa_array(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected component ad URLs hash to be an array");
        }
        *kanon_join_candidate.mutable_ad_component_render_urls_hash() =
            CborDecodeStringsArrToProto(kv.value);
        any_key_found = true;
        break;
      case 2:  // kReportingIdHash
        kanon_join_candidate.set_reporting_id_hash(CborDecodeString(kv.value));
        any_key_found = true;
        break;
      default:
        PS_VLOG(5)
            << "Following Key in KAnonWinnerJoinCandidates is unhandled: "
            << key;
    }
  }
  if (!any_key_found) {
    return absl::InvalidArgumentError(
        "No keys matching KAnonWinnerJoinCandidates found in passed in "
        "serialized CBOR input map");
  }
  return absl::OkStatus();
}

absl::Status CborDecodeGhostWinnerForTopLevelAuctionToProto(
    cbor_item_t* serialized_ghost,
    AuctionResult::KAnonGhostWinner::GhostWinnerForTopLevelAuction&
        ghost_winner) {
  if (!cbor_isa_map(serialized_ghost)) {
    return absl::InvalidArgumentError(
        "Expected ghost winner for top level auction to be a map");
  }
  absl::Span<struct cbor_pair> entries(cbor_map_handle(serialized_ghost),
                                       cbor_map_size(serialized_ghost));
  for (const auto& kv : entries) {
    if (!cbor_isa_string(kv.key)) {
      return absl::InvalidArgumentError(
          "Malformed key type in ghost winner for top level auction");
    }
    std::string key = CborDecodeString(kv.key);
    switch (FindKeyIndex<kNumGhostWinnerForTopLevelAuctionKeys>(
        kGhostWinnerForTopLevelAuctionKeys, key)) {
      case 0:  // kAdRenderUrlSmall
        ghost_winner.set_ad_render_url(CborDecodeString(kv.value));
        break;
      case 1:  // kAdComponentRenderUrls
        *ghost_winner.mutable_ad_component_render_urls() =
            CborDecodeStringsArrToProto(kv.value);
        break;
      case 2:  // kModifiedBid
        ghost_winner.set_modified_bid(cbor_float_get_float(kv.value));
        break;
      case 3:  // kBidCurrency
        ghost_winner.set_bid_currency(CborDecodeString(kv.value));
        break;
      case 4:  // kAdMetadata
        ghost_winner.set_ad_metadata(CborDecodeString(kv.value));
        break;
      case 5:  // kBuyerAndSellerReportingId
        ghost_winner.set_buyer_and_seller_reporting_id(
            CborDecodeString(kv.value));
        break;
      case 6:  // kBuyerReportingId
        ghost_winner.set_buyer_reporting_id(CborDecodeString(kv.value));
        break;
      case 7:  // kSelectableBuyerAndSellerReportingId
        ghost_winner.set_selectable_buyer_and_seller_reporting_id(
            CborDecodeString(kv.value));
        break;
      default:
        PS_VLOG(5)
            << "Unhandled key in ghost winner private aggregation signals: "
            << key;
    }
  }
  return absl::OkStatus();
}

absl::Status CborDecodePrivateAggregateSignalsToProto(
    cbor_item_t* serialized_signals,
    AuctionResult::KAnonGhostWinner::GhostWinnerPrivateAggregationSignals&
        signals) {
  if (!cbor_isa_map(serialized_signals)) {
    return absl::InvalidArgumentError(
        "Expected private aggregate signals to be a map");
  }
  absl::Span<struct cbor_pair> entries(cbor_map_handle(serialized_signals),
                                       cbor_map_size(serialized_signals));
  for (const auto& kv : entries) {
    if (!cbor_isa_string(kv.key)) {
      return absl::InvalidArgumentError(
          "Malformed key type in ghost winner private aggregation signals");
    }
    std::string key = CborDecodeString(kv.key);
    switch (FindKeyIndex<kNumGhostWinnerPrivateAggregationSignalsKeys>(
        kGhostWinnerPrivateAggregationSignalsKeys, key)) {
      case 0:  // kBucket
        signals.set_bucket(CborDecodeString(kv.value));
        break;
      case 1:  // kValue
        signals.set_value(cbor_get_int(kv.value));
        break;
      default:
        PS_VLOG(5)
            << "Unhandled key in ghost winner private aggregation signals: "
            << key;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<AuctionResult::KAnonGhostWinner>
CborDecodeKAnonGhostWinnerToProto(cbor_item_t* serialized_ghost) {
  if (!cbor_isa_map(serialized_ghost)) {
    return absl::InvalidArgumentError(
        "Expected each ghost winner candidate entry to be a map");
  }
  AuctionResult::KAnonGhostWinner kanon_ghost_winner;
  absl::Span<struct cbor_pair> entries(cbor_map_handle(serialized_ghost),
                                       cbor_map_size(serialized_ghost));
  for (const auto& kv : entries) {
    if (!cbor_isa_string(kv.key)) {
      return absl::InvalidArgumentError(
          "Malformed key type in kAnonGhostWinner");
    }
    std::string key = CborDecodeString(kv.key);
    switch (
        FindKeyIndex<kNumKAnonGhostWinnerKeys>(kKAnonGhostWinnerKeys, key)) {
      case 0:  // kKAnonJoinCandidates
        PS_RETURN_IF_ERROR(CborDecodeKAnonJoinCandidatesToProto(
            kv.value, *kanon_ghost_winner.mutable_k_anon_join_candidates()));
        break;
      case 1:  // kInterestGroupIndex
        kanon_ghost_winner.set_interest_group_index(cbor_get_int(kv.value));
        break;
      case 2:  // kOwner
        kanon_ghost_winner.set_owner(CborDecodeString(kv.value));
        break;
      case 3:  // kGhostWinnerPrivateAggregationSignals
        PS_RETURN_IF_ERROR(CborDecodePrivateAggregateSignalsToProto(
            kv.value,
            *kanon_ghost_winner
                 .mutable_ghost_winner_private_aggregation_signals()));
        break;
      case 4:  // kGhostWinnerForTopLevelAuction
        PS_RETURN_IF_ERROR(CborDecodeGhostWinnerForTopLevelAuctionToProto(
            kv.value,
            *kanon_ghost_winner.mutable_ghost_winner_for_top_level_auction()));

        break;
      default:
        PS_VLOG(5) << "Following Key in kAnonGhostWinner is unhandled: " << key;
    }
  }
  return kanon_ghost_winner;
}

absl::Status CborDecodeKAnonGhostWinnersToProto(
    cbor_item_t* serialized_candidates_arr, AuctionResult& auction_result) {
  absl::Span<cbor_item_t*> entries(cbor_array_handle(serialized_candidates_arr),
                                   cbor_array_size(serialized_candidates_arr));
  for (cbor_item_t* entry : entries) {
    PS_ASSIGN_OR_RETURN(auto decoded_kanon_ghost_winner,
                        CborDecodeKAnonGhostWinnerToProto(entry));
    *auction_result.mutable_k_anon_ghost_winners()->Add() =
        std::move(decoded_kanon_ghost_winner);
  }
  return absl::OkStatus();
}

}  // namespace

server_common::ConsentedDebugConfiguration DecodeConsentedDebugConfig(
    const cbor_item_t* root, ErrorAccumulator& error_accumulator,
    bool fail_fast) {
  server_common::ConsentedDebugConfiguration consented_debug_config;
  bool is_config_valid_type = IsTypeValid(
      &cbor_isa_map, root, kConsentedDebugConfig, kMap, error_accumulator);
  RETURN_IF_PREV_ERRORS(error_accumulator, /*fail_fast=*/!is_config_valid_type,
                        consented_debug_config);

  absl::Span<cbor_pair> entries(cbor_map_handle(root), cbor_map_size(root));
  for (const cbor_pair& entry : entries) {
    bool is_valid_key_type =
        IsTypeValid(&cbor_isa_string, entry.key, kConsentedDebugConfigKey,
                    kString, error_accumulator);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, consented_debug_config);
    if (!is_valid_key_type) {
      continue;
    }

    const int index =
        FindItemIndex(kConsentedDebugConfigKeys, DecodeCborString(entry.key));
    switch (index) {
      case 0: {  // IsConsented.
        bool is_valid_type = IsTypeValid(&cbor_is_bool, entry.value,
                                         kConsentedDebugConfigIsConsented,
                                         kString, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast,
                              consented_debug_config);
        if (is_valid_type) {
          consented_debug_config.set_is_consented(cbor_get_bool(entry.value));
        }
        break;
      }
      case 1: {  // Token.
        bool is_valid_type =
            IsTypeValid(&cbor_isa_string, entry.value,
                        kConsentedDebugConfigToken, kString, error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast,
                              consented_debug_config);
        if (is_valid_type) {
          consented_debug_config.set_token(DecodeCborString(entry.value));
        }
        break;
      }
      case 2: {  // IsDebugResponse.
        bool is_valid_type =
            IsTypeValid(&cbor_is_bool, entry.value,
                        kConsentedDebugConfigIsDebugInfoInResponse, kString,
                        error_accumulator);
        RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast,
                              consented_debug_config);
        if (is_valid_type) {
          consented_debug_config.set_is_debug_info_in_response(
              cbor_get_bool(entry.value));
        }
        break;
      }
    }
  }
  return consented_debug_config;
}

EncodedBuyerInputs DecodeBuyerInputKeys(
    cbor_item_t& compressed_encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
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

    std::string owner = DecodeCborString(interest_group.key);
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
    encoded_buyer_inputs.insert({std::move(owner), compressed_igs});
  }

  return encoded_buyer_inputs;
}

bool IsTypeValid(absl::AnyInvocable<bool(const cbor_item_t*)> is_valid_type,
                 const cbor_item_t* item, absl::string_view field_name,
                 absl::string_view expected_type,
                 ErrorAccumulator& error_accumulator,
                 server_common::SourceLocation location) {
  if (!is_valid_type(item)) {
    absl::string_view actual_type = kCborDataTypesLookup[item->type];
    std::string error = absl::StrFormat(kInvalidTypeError, field_name,
                                        expected_type, actual_type);
    PS_VLOG(kNoisyWarn) << "CBOR type validation failure at: "
                        << location.file_name() << ":" << location.line();
    error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error,
                                  ErrorCode::CLIENT_SIDE);
    return false;
  }

  return true;
}

std::string DecodeCborString(const cbor_item_t* item) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(item)),
                     cbor_string_length(item));
}

cbor_item_t* cbor_build_uint(uint input) {
  if (input <= std::numeric_limits<uint8_t>::max()) {
    return cbor_build_uint8(input);
  } else if (input <= std::numeric_limits<uint16_t>::max()) {
    return cbor_build_uint16(input);
  } else if (input <= std::numeric_limits<uint32_t>::max()) {
    return cbor_build_uint32(input);
  }
  return cbor_build_uint64(input);
}

cbor_item_t* cbor_build_negint(uint input) {
  if (input <= std::numeric_limits<uint8_t>::max()) {
    return cbor_build_negint8(input);
  } else if (input <= std::numeric_limits<uint16_t>::max()) {
    return cbor_build_negint16(input);
  } else if (input <= std::numeric_limits<uint32_t>::max()) {
    return cbor_build_negint32(input);
  }
  return cbor_build_negint64(input);
}

cbor_item_t* cbor_build_int(int input) {
  if (input < 0) {
    return cbor_build_negint(static_cast<uint>(-input - 1));
  }
  return cbor_build_uint(input);
}

absl::StatusOr<std::string> SerializeToCbor(cbor_item_t& cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size =
      cbor_serialized_size(&cbor_data_root);
  if (!cbor_serialized_data_size) {
    return absl::InternalError("CBOR data size too large");
  }

  std::string byte_string;
  byte_string.resize(cbor_serialized_data_size);
  if (cbor_serialize(&cbor_data_root,
                     reinterpret_cast<unsigned char*>(byte_string.data()),
                     cbor_serialized_data_size) == 0) {
    return absl::InternalError("Failed to serialize data to CBOR");
  }
  return byte_string;
}

template <typename T>
absl::StatusOr<double> EncodeDecodeFloatCbor(T input, cbor_float_width width) {
  std::string encoded;
  switch (width) {
    case CBOR_FLOAT_16: {
      ScopedCbor candidate(cbor_build_float2(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
    case CBOR_FLOAT_32: {
      ScopedCbor candidate(cbor_build_float4(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
    default: {
      ScopedCbor candidate(cbor_build_float8(input));
      PS_ASSIGN_OR_RETURN(encoded, SerializeToCbor(**candidate));
    } break;
  }

  cbor_load_result cbor_result;
  ScopedCbor loaded_data(
      cbor_load(reinterpret_cast<unsigned char*>(encoded.data()),
                encoded.size(), &cbor_result));
  if (*loaded_data == nullptr || cbor_result.error.code != CBOR_ERR_NONE) {
    return absl::InternalError("Failed to load CBOR encoded float");
  }

  return cbor_float_get_float(*loaded_data);
}

absl::StatusOr<cbor_item_t*> cbor_build_float(double input) {
  PS_ASSIGN_OR_RETURN(double decoded_double,
                      EncodeDecodeFloatCbor(input, CBOR_FLOAT_64));
  PS_ASSIGN_OR_RETURN(float decoded_single,
                      EncodeDecodeFloatCbor(input, CBOR_FLOAT_32));
  if (AreFloatsEqual(decoded_double, decoded_single)) {
    PS_ASSIGN_OR_RETURN(float decoded_half,
                        EncodeDecodeFloatCbor(input, CBOR_FLOAT_16));
    if (AreFloatsEqual(decoded_single, decoded_half)) {
      PS_VLOG(5) << "Original input: " << input
                 << ", encoded as half precision float: " << decoded_half;
      return cbor_build_float2(input);
    }
    PS_VLOG(5) << "Original input: " << input
               << ", encoded as single precision float: " << decoded_single;
    return cbor_build_float4(input);
  }
  PS_VLOG(5) << "Original input: " << input
             << ", encoded as double precision float: " << decoded_double;
  return cbor_build_float8(input);
}

absl::Status CborSerializeUpdateGroups(const UpdateGroupMap& update_groups,
                                       ErrorHandler error_handler,
                                       cbor_item_t& root) {
  ScopedCbor serialized_group_map(cbor_new_definite_map(update_groups.size()));
  std::set<absl::string_view, decltype(kComparator)> ordered_origins(
      kComparator);
  // NOLINTNEXTLINE
  for (const auto& [origin, unused] : update_groups) {
    ordered_origins.insert(origin);
  }
  for (absl::string_view origin : ordered_origins) {
    const UpdateInterestGroupList& updates = update_groups.at(origin);

    ScopedCbor update_group_array(
        cbor_new_definite_array(updates.interest_groups().size()));
    for (const UpdateInterestGroup& update : updates.interest_groups()) {
      ScopedCbor serialized_update_map(cbor_new_definite_map(2));
      PS_RETURN_IF_ERROR(CborSerializeInt(kIndex, update.index(), error_handler,
                                          **serialized_update_map));
      PS_RETURN_IF_ERROR(CborSerializeInt(
          kUpdateIfOlderThanMs, update.update_if_older_than_ms(), error_handler,
          **serialized_update_map));
      if (!cbor_array_push(*update_group_array, *serialized_update_map)) {
        error_handler(grpc::Status(
            grpc::INTERNAL, absl::StrCat("Failed to add interest group update "
                                         "entry to array for owner: ",
                                         origin)));
        return absl::InternalError("");
      }
    }

    struct cbor_pair owner_update_groups_entry = {
        .key = cbor_move(cbor_build_stringn(origin.data(), origin.size())),
        .value = *update_group_array};

    if (!cbor_map_add(*serialized_group_map, owner_update_groups_entry)) {
      error_handler(grpc::Status(
          grpc::INTERNAL,
          "Failed to serialize an <origin, update group array> pair to CBOR"));
      return absl::InternalError("");
    }
  }

  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kUpdateGroups, sizeof(kUpdateGroups) - 1)),
                         .value = *serialized_group_map};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize ", kUpdateGroups, " to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializeBiddingGroups(const BiddingGroupMap& bidding_groups,
                                        ErrorHandler error_handler,
                                        cbor_item_t& root) {
  ScopedCbor serialized_group_map(cbor_new_definite_map(bidding_groups.size()));
  // Order keys by length first and then lexicographically.
  std::set<absl::string_view, decltype(kComparator)> ordered_origins(
      kComparator);
  // NOLINTNEXTLINE
  for (const auto& [origin, unused] : bidding_groups) {
    ordered_origins.insert(origin);
  }
  for (const auto& origin : ordered_origins) {
    const auto& group_indices = bidding_groups.at(origin);
    ScopedCbor serialized_group_indices(
        cbor_new_definite_array(group_indices.index_size()));
    for (int32_t index : group_indices.index()) {
      if (!cbor_array_push(*serialized_group_indices,
                           cbor_move(cbor_build_uint(index)))) {
        error_handler(
            grpc::Status(grpc::INTERNAL,
                         "Failed to serialize a bidding group index to CBOR"));
        return absl::InternalError("");
      }
    }
    struct cbor_pair kv = {
        .key = cbor_move(cbor_build_stringn(origin.data(), origin.size())),
        .value = *serialized_group_indices};
    if (!cbor_map_add(*serialized_group_map, kv)) {
      error_handler(grpc::Status(
          grpc::INTERNAL,
          "Failed to serialize an <origin, bidding group array> pair to CBOR"));
      return absl::InternalError("");
    }
  }
  struct cbor_pair kv = {.key = cbor_move(cbor_build_stringn(
                             kBiddingGroups, sizeof(kBiddingGroups) - 1)),
                         .value = *serialized_group_map};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize ", kBiddingGroups, " to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::Status CborSerializeInteractionReportingUrls(
    const InteractionUrlMap& interaction_url_map, ErrorHandler error_handler,
    cbor_item_t& root) {
  ScopedCbor serialized_interaction_url_map(
      cbor_new_definite_map(interaction_url_map.size()));
  std::set<absl::string_view, decltype(kComparator)> ordered_events(
      kComparator);
  for (const auto& [event, unused] : interaction_url_map) {
    ordered_events.insert(event);
  }
  for (const auto& event : ordered_events) {
    PS_RETURN_IF_ERROR(AddKVToMap(event, interaction_url_map.at(event),
                                  error_handler,
                                  **serialized_interaction_url_map));
  }
  struct cbor_pair kv = {
      .key = cbor_move(cbor_build_stringn(
          kInteractionReportingUrls, sizeof(kInteractionReportingUrls) - 1)),
      .value = *serialized_interaction_url_map};
  if (!cbor_map_add(&root, kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ",
                                     kInteractionReportingUrls, " to CBOR")));
    return absl::InternalError("");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> Encode(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupMap& bidding_group_map,
    const UpdateGroupMap& update_group_map,
    const std::optional<AuctionResult::Error>& error,
    ErrorHandler error_handler,
    const KAnonAuctionResultData* kanon_auction_result_data) {
  // CBOR data's root handle. When serializing the auction result to CBOR, we
  // use this handle to keep the temporary data.
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumAuctionResultKeys));
  auto* cbor_internal = cbor_data_root.get();

  if (error.has_value()) {
    PS_RETURN_IF_ERROR(
        CborSerializeError(*error, error_handler, *cbor_internal));
  } else if (high_score.has_value()) {
    PS_RETURN_IF_ERROR(CborSerializeScoreAdResponse(
        *high_score, bidding_group_map, update_group_map,
        kanon_auction_result_data, error_handler, *cbor_internal));
  } else {
    PS_RETURN_IF_ERROR(
        CborSerializeBool(kChaff, true, error_handler, *cbor_internal));
    if (kanon_auction_result_data != nullptr) {
      PS_RETURN_IF_ERROR(CborSerializeKAnonGhostWinners(
          kKAnonGhostWinners, kanon_auction_result_data->kanon_ghost_winners,
          error_handler, *cbor_internal));
    }
  }

  return GetCborSerializedAuctionResult(error_handler, *cbor_internal);
}

absl::StatusOr<std::string> EncodeComponent(
    absl::string_view top_level_seller,
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const BiddingGroupMap& bidding_group_map,
    const UpdateGroupMap& update_group_map,
    const std::optional<AuctionResult::Error>& error,
    ErrorHandler error_handler) {
  // CBOR data's root handle. When serializing the auction result to CBOR, we
  // use this handle to keep the temporary data.
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumAuctionResultKeys));
  auto* cbor_internal = cbor_data_root.get();

  if (error.has_value()) {
    PS_RETURN_IF_ERROR(
        CborSerializeError(*error, error_handler, *cbor_internal));
  } else if (high_score.has_value()) {
    PS_RETURN_IF_ERROR(CborSerializeComponentScoreAdResponse(
        top_level_seller, *high_score, bidding_group_map, update_group_map,
        error_handler, *cbor_internal));
  } else {
    PS_RETURN_IF_ERROR(
        CborSerializeBool(kChaff, true, error_handler, *cbor_internal));
  }

  return GetCborSerializedAuctionResult(error_handler, *cbor_internal);
}

DecodedBuyerInputs DecodeBuyerInputs(
    const EncodedBuyerInputs& encoded_buyer_inputs,
    ErrorAccumulator& error_accumulator, bool fail_fast) {
  DecodedBuyerInputs decoded_buyer_inputs;
  for (const auto& [owner, compressed_buyer_input] : encoded_buyer_inputs) {
    BuyerInput buyer_input = DecodeBuyerInput(owner, compressed_buyer_input,
                                              error_accumulator, fail_fast);
    RETURN_IF_PREV_ERRORS(error_accumulator, fail_fast, decoded_buyer_inputs);

    decoded_buyer_inputs.insert({owner, std::move(buyer_input)});
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

absl::Status CborSerializeReportingUrls(
    absl::string_view key,
    const WinReportingUrls::ReportingUrls& reporting_urls,
    ErrorHandler error_handler, cbor_item_t& root) {
  int key_count = 0;
  if (!reporting_urls.reporting_url().empty()) {
    key_count++;
  }
  if (!reporting_urls.interaction_reporting_urls().empty()) {
    key_count++;
  }
  if (key_count == 0) {
    return absl::OkStatus();
  }
  ScopedCbor serialized_reporting_urls(
      cbor_new_definite_map(kNumReportingUrlsKeys));
  if (!reporting_urls.reporting_url().empty()) {
    PS_RETURN_IF_ERROR(AddKVToMap(kReportingUrl, reporting_urls.reporting_url(),
                                  error_handler, **serialized_reporting_urls));
  }
  if (!reporting_urls.interaction_reporting_urls().empty()) {
    PS_RETURN_IF_ERROR(CborSerializeInteractionReportingUrls(
        reporting_urls.interaction_reporting_urls(), error_handler,
        **serialized_reporting_urls));
  }
  struct cbor_pair serialized_reporting_urls_kv = {
      .key = cbor_move(cbor_build_stringn(key.data(), key.size())),
      .value = *serialized_reporting_urls,
  };

  if (!cbor_map_add(&root, serialized_reporting_urls_kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL, absl::StrCat("Failed to serialize ", key, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::Status CborSerializeWinReportingUrls(
    const WinReportingUrls& win_reporting_urls, ErrorHandler error_handler,
    cbor_item_t& root) {
  if (!win_reporting_urls.has_buyer_reporting_urls() &&
      !win_reporting_urls.has_top_level_seller_reporting_urls()) {
    return absl::OkStatus();
  }
  ScopedCbor serialized_win_reporting_urls(
      cbor_new_definite_map(kNumWinReportingUrlsKeys));
  if (win_reporting_urls.has_buyer_reporting_urls()) {
    PS_RETURN_IF_ERROR(CborSerializeReportingUrls(
        kBuyerReportingUrls, win_reporting_urls.buyer_reporting_urls(),
        error_handler, **serialized_win_reporting_urls));
  }
  if (win_reporting_urls.has_top_level_seller_reporting_urls()) {
    PS_RETURN_IF_ERROR(CborSerializeReportingUrls(
        kTopLevelSellerReportingUrls,
        win_reporting_urls.top_level_seller_reporting_urls(), error_handler,
        **serialized_win_reporting_urls));
  }
  if (win_reporting_urls.has_component_seller_reporting_urls()) {
    PS_RETURN_IF_ERROR(CborSerializeReportingUrls(
        kComponentSellerReportingUrls,
        win_reporting_urls.component_seller_reporting_urls(), error_handler,
        **serialized_win_reporting_urls));
  }
  struct cbor_pair serialized_win_reporting_urls_kv = {
      .key = cbor_move(
          cbor_build_stringn(kWinReportingUrls, sizeof(kWinReportingUrls) - 1)),
      .value = *serialized_win_reporting_urls,
  };
  if (!cbor_map_add(&root, serialized_win_reporting_urls_kv)) {
    error_handler(grpc::Status(
        grpc::INTERNAL,
        absl::StrCat("Failed to serialize ", kWinReportingUrls, " to CBOR")));
    return absl::InternalError("");
  }

  return absl::OkStatus();
}

absl::StatusOr<AuctionResult> CborDecodeAuctionResultToProto(
    absl::string_view serialized_input) {
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
      case 0: {  // kScore
        if (!cbor_isa_float_ctrl(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected score value to be a float");
        }
        auto decoded_value = cbor_float_get_float4(kv.value);
        auction_result.set_score(decoded_value);
      } break;
      case 1: {  // kBid
        if (!cbor_isa_float_ctrl(kv.value)) {
          return absl::InvalidArgumentError("Expected bid value to be a float");
        }
        auto decoded_value = cbor_float_get_float4(kv.value);
        auction_result.set_bid(decoded_value);
      } break;
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
            CborDecodeStringsArrToProto(kv.value);
        break;
      case 8: {  // kError
        if (!cbor_isa_map(kv.value)) {
          return absl::InvalidArgumentError("Expected error value to be a map");
        }
        AuctionResult::Error error;
        PS_ASSIGN_OR_RETURN(error, CborDecodeErrorToProto(kv.value));
        *auction_result.mutable_error() = std::move(error);
      } break;
      case 9: {  // kWinReportingUrls
        if (!cbor_isa_map(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected winReportingUrls value to be a map");
        }
        PS_RETURN_IF_ERROR(
            CborDecodeReportingUrlsToProto(kv.value, auction_result))
            << absl::StrFormat("Error decoding winReportingUrls");
      } break;
      case 10: {  // kAdMetadata
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected AdMetadata value to be a string");
        }
        auction_result.set_ad_metadata(CborDecodeString(kv.value));
      } break;
      case 11: {  // kTopLevelSeller
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected Top Level Seller value to be a string");
        }
        auction_result.set_top_level_seller(CborDecodeString(kv.value));
      } break;
      case 12: {  // kBidCurrency
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected Bid Currency value to be a string");
        }
        auction_result.set_bid_currency(CborDecodeString(kv.value));
      } break;
      case 13: {  // kBuyerReportingId
        if (!cbor_isa_string(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected buyer reporting id to be a string");
        }
        auction_result.set_buyer_reporting_id(CborDecodeString(kv.value));
      } break;
      case 14: {  // kKAnonGhostWinners
        if (!cbor_isa_array(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected kAnonGhostWinners value to be an array");
        }
        PS_RETURN_IF_ERROR(
            CborDecodeKAnonGhostWinnersToProto(kv.value, auction_result))
            << "Error decoding kAnonGhostWinners";
      } break;
      case 15: {  // kKAnonWinnerJoinCandidates
        if (!cbor_isa_map(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected kAnonWinnerJoinCandidates value to be a map");
        }
        PS_RETURN_IF_ERROR(CborDecodeKAnonJoinCandidatesToProto(
            kv.value, *auction_result.mutable_k_anon_winner_join_candidates()))
            << "Error decoding kAnonWinnerJoinCandidates";
      } break;
      case 16: {  // kKAnonWinnerPositionalIndex
        if (!cbor_is_int(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected KAnonWinnerPositionalIndex value to be an int");
        }
        auction_result.set_k_anon_winner_positional_index(
            cbor_get_int(kv.value));
      } break;
      case 17: {  // kUpdateGroups
        if (!cbor_isa_map(kv.value)) {
          return absl::InvalidArgumentError(
              "Expected updateGroups value to be a map ");
        }
        absl::StatusOr<UpdateGroupMap> update_groups;
        PS_ASSIGN_OR_RETURN(update_groups,
                            CborDecodeUpdateGroupMapToProto(kv.value));
        *auction_result.mutable_update_groups() = std::move(*update_groups);
      } break;
      default:
        // Unexpected key in the auction result CBOR
        PS_VLOG(5) << "Serialized CBOR auction result has an unknown root key: "
                   << key;
    }
  }

  return auction_result;
}

}  // namespace privacy_sandbox::bidding_auction_servers
