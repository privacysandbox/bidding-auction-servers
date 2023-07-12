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

#include <set>
#include <utility>
#include <vector>

#include <google/protobuf/util/message_differencer.h>
#include <include/gmock/gmock-actions.h>

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "glog/logging.h"
#include "services/common/compression/gzip.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr char kSamplePublisher[] = "foo_publisher";
inline constexpr char kSampleIgName[] = "foo_ig_name";
inline constexpr char kSampleBiddingSignalKey1[] = "bidding_signal_key_1";
inline constexpr char kSampleBiddingSignalKey2[] = "bidding_signal_key_2";
inline constexpr char kSampleAdRenderId1[] = "ad_render_id_1";
inline constexpr char kSampleAdRenderId2[] = "ad_render_id_2";
inline constexpr char kSampleAdComponentRenderId1[] =
    "ad_component_render_id_1";
inline constexpr char kSampleAdComponentRenderId2[] =
    "ad_component_render_id_2";
inline constexpr char kSampleUserBiddingSignals[] =
    R"(["this should be a", "JSON array", "that gets parsed", "into separate list values"])";
inline constexpr int kSampleJoinCount = 1;
inline constexpr int kSampleBidCount = 2;
inline constexpr int kSampleRecency = 3;
inline constexpr char kSampleIgOwner[] = "foo_owner";
inline constexpr char kSampleGenerationId[] =
    "6fa459ea-ee8a-3ca4-894e-db77e160355e";
inline constexpr char kSampleErrorMessage[] = "BadError";
inline constexpr int32_t kSampleErrorCode = 400;

using ErrorVisibility::CLIENT_VISIBLE;

cbor_pair BuildStringMapPair(absl::string_view key, absl::string_view value) {
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(cbor_build_stringn(value.data(), value.size()))};
}

cbor_pair BuildBoolMapPair(absl::string_view key, bool value) {
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(cbor_build_bool(value))};
}

cbor_pair BuildBytestringMapPair(absl::string_view key,
                                 absl::string_view value) {
  cbor_data data = reinterpret_cast<const unsigned char*>(value.data());
  cbor_item_t* bytestring = cbor_build_bytestring(data, value.size());
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(bytestring)};
}

cbor_pair BuildIntMapPair(absl::string_view key, uint32_t value) {
  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(cbor_build_uint64(value))};
}

cbor_pair BuildStringArrayMapPair(
    absl::string_view key, const std::vector<absl::string_view>& values) {
  cbor_item_t* array = cbor_new_definite_array(values.size());
  for (auto const& value : values) {
    cbor_item_t* array_entry = cbor_build_stringn(value.data(), value.size());
    EXPECT_TRUE(cbor_array_push(array, cbor_move(array_entry)));
  }

  return {cbor_move(cbor_build_stringn(key.data(), key.size())),
          cbor_move(array)};
}

struct cbor_item_t* BuildSampleCborInterestGroup() {
  cbor_item_t* interest_group = cbor_new_definite_map(kNumInterestGroupKeys);
  EXPECT_TRUE(
      cbor_map_add(interest_group, BuildStringMapPair(kName, kSampleIgName)));
  EXPECT_TRUE(cbor_map_add(
      interest_group, BuildStringArrayMapPair(kBiddingSignalsKeys,
                                              {kSampleBiddingSignalKey1,
                                               kSampleBiddingSignalKey2})));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringMapPair(kUserBiddingSignals, kSampleUserBiddingSignals)));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringArrayMapPair(kAds, {kSampleAdRenderId1, kSampleAdRenderId2})));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringArrayMapPair(kAdComponents, {kSampleAdComponentRenderId1,
                                              kSampleAdComponentRenderId2})));

  cbor_item_t* browser_signals = cbor_new_definite_map(kNumBrowserSignalKeys);
  EXPECT_TRUE(cbor_map_add(browser_signals,
                           BuildIntMapPair(kJoinCount, kSampleJoinCount)));
  EXPECT_TRUE(cbor_map_add(browser_signals,
                           BuildIntMapPair(kBidCount, kSampleBidCount)));
  EXPECT_TRUE(
      cbor_map_add(browser_signals, BuildIntMapPair(kRecency, kSampleRecency)));

  // Build prevWins arrays.
  cbor_item_t* child_arr_1 = cbor_new_definite_array(2);
  EXPECT_TRUE(cbor_array_push(child_arr_1, cbor_move(cbor_build_uint64(-20))));
  EXPECT_TRUE(cbor_array_push(
      child_arr_1, cbor_move(cbor_build_stringn(
                       kSampleAdRenderId1, sizeof(kSampleAdRenderId1) - 1))));
  cbor_item_t* child_arr_2 = cbor_new_definite_array(2);
  EXPECT_TRUE(cbor_array_push(child_arr_2, cbor_move(cbor_build_uint64(-100))));
  EXPECT_TRUE(cbor_array_push(
      child_arr_2, cbor_move(cbor_build_stringn(
                       kSampleAdRenderId2, sizeof(kSampleAdRenderId2) - 1))));

  cbor_item_t* parent_arr = cbor_new_definite_array(2);
  EXPECT_TRUE(cbor_array_push(parent_arr, cbor_move(child_arr_1)));
  EXPECT_TRUE(cbor_array_push(parent_arr, cbor_move(child_arr_2)));

  EXPECT_TRUE(cbor_map_add(
      browser_signals,
      {cbor_move(cbor_build_stringn(kPrevWins, sizeof(kPrevWins) - 1)),
       cbor_move(parent_arr)}));
  cbor_item_t* browser_signals_key =
      cbor_build_stringn(kBrowserSignals, sizeof(kBrowserSignals) - 1);
  EXPECT_TRUE(cbor_map_add(interest_group, {cbor_move(browser_signals_key),
                                            cbor_move(browser_signals)}));
  return cbor_move(interest_group);
}

cbor_item_t* CompressInterestGroups(const ScopedCbor& ig_array) {
  std::string serialized_ig_array = SerializeCbor(*ig_array);
  absl::StatusOr<std::string> compressed_ig = GzipCompress(serialized_ig_array);
  cbor_data my_cbor_data =
      reinterpret_cast<const unsigned char*>(compressed_ig->data());
  return cbor_build_bytestring(my_cbor_data, compressed_ig->size());
}

bool ContainsClientError(const ErrorAccumulator::ErrorMap& error_map,
                         absl::string_view target) {
  auto it = error_map.find(ErrorCode::CLIENT_SIDE);
  if (it == error_map.end()) {
    return false;
  }

  for (const auto& observed_err : it->second) {
    if (absl::StrContains(observed_err, target)) {
      return true;
    }
  }

  return false;
}

TEST(ChromeRequestUtils, Decode_Success) {
  ScopedCbor protected_audience_input(
      cbor_new_definite_map(kNumRequestRootKeys));
  EXPECT_TRUE(cbor_map_add(*protected_audience_input,
                           BuildStringMapPair(kPublisher, kSamplePublisher)));
  EXPECT_TRUE(
      cbor_map_add(*protected_audience_input,
                   BuildStringMapPair(kGenerationId, kSampleGenerationId)));
  EXPECT_TRUE(cbor_map_add(*protected_audience_input,
                           BuildBoolMapPair(kDebugReporting, true)));

  ScopedCbor ig_array(cbor_new_definite_array(1));
  EXPECT_TRUE(cbor_array_push(*ig_array, BuildSampleCborInterestGroup()));
  cbor_item_t* ig_bytestring = CompressInterestGroups(ig_array);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(cbor_map_add(interest_group_data_map,
                           {cbor_move(cbor_build_stringn(
                                kSampleIgOwner, sizeof(kSampleIgOwner) - 1)),
                            cbor_move(ig_bytestring)}));
  EXPECT_TRUE(cbor_map_add(*protected_audience_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_audience_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAudienceInput actual = Decode(serialized_cbor, error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());

  ProtectedAudienceInput expected;
  expected.set_publisher_name(kSamplePublisher);
  expected.set_enable_debug_reporting(true);
  expected.set_generation_id(kSampleGenerationId);

  BuyerInput_InterestGroup expected_ig;
  expected_ig.set_name(kSampleIgName);
  expected_ig.add_bidding_signals_keys(kSampleBiddingSignalKey1);
  expected_ig.add_bidding_signals_keys(kSampleBiddingSignalKey2);
  expected_ig.add_ad_render_ids(kSampleAdRenderId1);
  expected_ig.add_ad_render_ids(kSampleAdRenderId2);
  expected_ig.add_component_ads(kSampleAdComponentRenderId1);
  expected_ig.add_component_ads(kSampleAdComponentRenderId2);
  expected_ig.set_user_bidding_signals(kSampleUserBiddingSignals);

  BrowserSignals* signals = expected_ig.mutable_browser_signals();
  signals->set_join_count(kSampleJoinCount);
  signals->set_bid_count(kSampleBidCount);
  signals->set_recency(kSampleRecency);
  std::string prev_wins_json_str = absl::StrFormat(
      R"([[-20,"%s"],[-100,"%s"]])", kSampleAdRenderId1, kSampleAdRenderId2);
  signals->set_prev_wins(prev_wins_json_str);

  BuyerInput buyer_input;
  *buyer_input.add_interest_groups() = expected_ig;
  expected_ig.mutable_browser_signals()->set_prev_wins(prev_wins_json_str);
  google::protobuf::Map<std::string, BuyerInput> buyer_inputs;
  buyer_inputs.emplace(kSampleIgOwner, std::move(buyer_input));

  absl::StatusOr<EncodedBuyerInputs> encoded_buyer_inputs =
      GetEncodedBuyerInputMap(buyer_inputs);
  ASSERT_TRUE(encoded_buyer_inputs.ok()) << encoded_buyer_inputs.status();
  *expected.mutable_buyer_input() = std::move(*encoded_buyer_inputs);

  std::string papi_differences;
  google::protobuf::util::MessageDifferencer papi_differencer;
  papi_differencer.ReportDifferencesToString(&papi_differences);
  // Note that this comparison is fragile because the CBOR encoding depends
  // on the order in which the data was created and if we have a difference
  // between the order in which the data items are added in the test vs how
  // they are added in the implementation, we will start to see failures.
  if (!papi_differencer.Compare(actual, expected)) {
    VLOG(1) << "Actual proto does not match expected proto";
    VLOG(1) << "\nExpected:\n" << expected.DebugString();
    VLOG(1) << "\nActual:\n" << actual.DebugString();
    VLOG(1) << "\nFound differences in ProtectedAudienceInput:\n"
            << papi_differences;

    auto expected_buyer_inputs =
        DecodeBuyerInputs(expected.buyer_input(), error_accumulator);
    ASSERT_FALSE(error_accumulator.HasErrors());
    ASSERT_EQ(expected_buyer_inputs.size(), 1);
    const auto& [expected_buyer, expected_buyer_input] =
        *expected_buyer_inputs.begin();

    std::string bi_differences;
    google::protobuf::util::MessageDifferencer bi_differencer;
    bi_differencer.ReportDifferencesToString(&bi_differences);
    VLOG(1) << "\nExpected BuyerInput:\n" << expected_buyer_input.DebugString();
    BuyerInput actual_buyer_input =
        DecodeBuyerInputs(actual.buyer_input(), error_accumulator)
            .begin()
            ->second;
    VLOG(1) << "\nActual BuyerInput:\n" << actual_buyer_input.DebugString();
    EXPECT_TRUE(
        !bi_differencer.Compare(actual_buyer_input, expected_buyer_input))
        << bi_differences;
    FAIL();
  }
}

TEST(ChromeRequestUtils, Decode_FailOnWrongType) {
  ScopedCbor root(cbor_build_stringn("string", 6));
  std::string serialized_cbor = SerializeCbor(*root);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAudienceInput actual = Decode(serialized_cbor, error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());

  const std::string expected_error = absl::StrFormat(
      kInvalidTypeError, "ProtectedAudienceInput", kMap, kString);
  EXPECT_TRUE(ContainsClientError(error_accumulator.GetErrors(CLIENT_VISIBLE),
                                  expected_error));
}

TEST(ChromeRequestUtils, Decode_FailOnUnsupportedVersion) {
  ScopedCbor protected_audience_input(cbor_new_definite_map(1));
  EXPECT_TRUE(
      cbor_map_add(*protected_audience_input, BuildIntMapPair(kVersion, 999)));
  std::string serialized_cbor = SerializeCbor(*protected_audience_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAudienceInput actual = Decode(serialized_cbor, error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());

  const std::string expected_error =
      absl::StrFormat(kUnsupportedSchemaVersionError, 999);
  EXPECT_TRUE(ContainsClientError(error_accumulator.GetErrors(CLIENT_VISIBLE),
                                  expected_error));
}

TEST(ChromeRequestUtils, Decode_FailOnMalformedCompresedBytestring) {
  ScopedCbor protected_audience_input(cbor_new_definite_map(1));
  ScopedCbor ig_array(cbor_new_definite_array(1));
  EXPECT_TRUE(cbor_array_push(*ig_array, BuildSampleCborInterestGroup()));
  ScopedCbor ig_bytestring = ScopedCbor(CompressInterestGroups(ig_array));

  // Cut the compressed string in half and try that.
  std::string compressed_split(reinterpret_cast<char*>(ig_bytestring->data),
                               cbor_bytestring_length(*ig_bytestring) / 2);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(
      cbor_map_add(interest_group_data_map,
                   BuildBytestringMapPair(kSampleIgOwner, compressed_split)));
  EXPECT_TRUE(cbor_map_add(*protected_audience_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_audience_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  // The main decoding method for protected audience input doesn't decompress
  // and decode the BuyerInput. The latter is handled separately.
  ProtectedAudienceInput actual = Decode(serialized_cbor, error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());

  absl::flat_hash_map<absl::string_view, BuyerInput> buyer_inputs =
      DecodeBuyerInputs(actual.buyer_input(), error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());
  EXPECT_TRUE(ContainsClientError(error_accumulator.GetErrors(CLIENT_VISIBLE),
                                  kMalformedCompressedBytestring));
}

TEST(ChromeResponseUtils, VerifyCborEncoding) {
  // Setup a winning bid.
  const std::string interest_group = "interest_group";
  const std::string ad_render_url = "https://ad-found-here.com/ad-1";
  const std::string interest_group_owner = "https://ig-owner.com:1234";
  const int ig_index = 2;
  const float bid = 10.21;
  const float desirability = 2.35;
  ScoreAdsResponse::AdScore winner;
  winner.set_render(ad_render_url);
  winner.set_desirability(desirability);
  winner.set_buyer_bid(bid);
  winner.set_interest_group_name(interest_group);
  winner.set_interest_group_owner(interest_group_owner);

  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(ig_index);
  bidding_group_map.try_emplace(interest_group_owner, std::move(ig_indices));

  absl::StatusOr<std::vector<unsigned char>> response_with_cbor =
      Encode(winner, std::move(bidding_group_map), /*error=*/std::nullopt,
             [](absl::string_view error) {});
  EXPECT_TRUE(response_with_cbor.ok());

  std::string byte_string =
      std::string(reinterpret_cast<char*>(response_with_cbor->data()),
                  response_with_cbor->size());
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(byte_string);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  // Verify that the decoded result has all the bidding groups correctly set.
  EXPECT_EQ(decoded_result->bidding_groups().size(), 1);
  const auto& [observed_owner, observed_ig_indices] =
      *decoded_result->bidding_groups().begin();
  EXPECT_EQ(observed_owner, interest_group_owner);
  EXPECT_EQ(observed_ig_indices.index_size(), 1);
  EXPECT_EQ(observed_ig_indices.index(0), ig_index);

  // Verify that the decoded result has the winning ad correctly set.
  EXPECT_EQ(decoded_result->ad_render_url(), ad_render_url);
  EXPECT_EQ(decoded_result->bid(), bid);
  EXPECT_EQ(decoded_result->score(), desirability);
  EXPECT_EQ(decoded_result->interest_group_name(), interest_group);
  EXPECT_EQ(decoded_result->interest_group_owner(), interest_group_owner);
}

TEST(ChromeResponseUtils, VerifCBOREncodedError) {
  ScoreAdsResponse::AdScore winner;
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;

  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);
  absl::StatusOr<std::vector<unsigned char>> response_with_cbor =
      Encode(winner, std::move(bidding_group_map), error,
             [](absl::string_view error) {});

  std::string byte_string =
      std::string(reinterpret_cast<char*>(response_with_cbor->data()),
                  response_with_cbor->size());
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(byte_string);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();
  EXPECT_EQ(decoded_result->error().message(), kSampleErrorMessage);
  EXPECT_EQ(decoded_result->error().code(), kSampleErrorCode);
}

std::string ErrStr(absl::string_view field_name,
                   absl::string_view expected_type,
                   absl::string_view observed_type) {
  return absl::StrFormat(kInvalidTypeError, field_name, expected_type,
                         observed_type);
}

TEST(WebRequestUtils, Decode_FailsAndGetsAllErrors) {
  ScopedCbor protected_audience_input(
      cbor_new_definite_map(kNumRequestRootKeys));

  std::set<std::string> expected_errors;

  // Malformed key type for publisher.
  cbor_pair publisher_kv = {
      cbor_move(cbor_build_bytestring(reinterpret_cast<cbor_data>(kPublisher),
                                      sizeof(kPublisher) - 1)),
      cbor_move(
          cbor_build_stringn(kSamplePublisher, sizeof(kSamplePublisher) - 1))};
  EXPECT_TRUE(cbor_map_add(*protected_audience_input, std::move(publisher_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kRootCborKey,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypeByteString));

  // Malformed value type for generation id.
  cbor_pair gen_id_kv = {
      cbor_move(cbor_build_stringn(kGenerationId, sizeof(kGenerationId) - 1)),
      cbor_move(cbor_build_bytestring(
          reinterpret_cast<cbor_data>(kSampleGenerationId),
          sizeof(kSampleGenerationId) - 1))};
  EXPECT_TRUE(cbor_map_add(*protected_audience_input, std::move(gen_id_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kGenerationId,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypeByteString));

  ScopedCbor ig_array(cbor_new_definite_array(1));
  // Malformed interest group fields.
  cbor_item_t* interest_group = cbor_new_definite_map(kNumInterestGroupKeys);
  // Malformed interest group name field.
  cbor_pair ig_name_kv = {
      cbor_move(cbor_build_stringn(kName, sizeof(kName) - 1)),
      cbor_move(cbor_build_uint32(1))};
  EXPECT_TRUE(cbor_map_add(interest_group, std::move(ig_name_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kIgName,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypePositiveInt));
  // Malformed browser signals.
  cbor_item_t* browser_signals_array = cbor_new_definite_array(1);
  EXPECT_TRUE(
      cbor_array_push(browser_signals_array, cbor_move(cbor_build_uint32(1))));
  cbor_pair browser_signals_kv = {
      cbor_move(cbor_build_stringn(kBrowserSignalsKey,
                                   sizeof(kBrowserSignalsKey) - 1)),
      cbor_move(browser_signals_array)};
  EXPECT_TRUE(cbor_map_add(interest_group, std::move(browser_signals_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kBrowserSignalsKey,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypePositiveInt));
  // Malformed bidding signals.
  cbor_item_t* bidding_signals_array = cbor_new_definite_array(1);
  EXPECT_TRUE(
      cbor_array_push(bidding_signals_array, cbor_move(cbor_build_uint32(1))));
  cbor_pair bidding_signals_kv = {
      cbor_move(cbor_build_stringn(kBiddingSignalsKeys,
                                   sizeof(kBiddingSignalsKeys) - 1)),
      cbor_move(bidding_signals_array)};
  EXPECT_TRUE(cbor_map_add(interest_group, std::move(bidding_signals_kv)));
  expected_errors.emplace(ErrStr(/*field_name=*/kIgBiddingSignalKeysEntry,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypePositiveInt));

  EXPECT_TRUE(cbor_array_push(*ig_array, cbor_move(interest_group)));
  cbor_item_t* ig_bytestring = CompressInterestGroups(ig_array);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(cbor_map_add(interest_group_data_map,
                           {cbor_move(cbor_build_stringn(
                                kSampleIgOwner, sizeof(kSampleIgOwner) - 1)),
                            cbor_move(ig_bytestring)}));
  EXPECT_TRUE(cbor_map_add(*protected_audience_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_audience_input);
  ContextLogger logger;
  ErrorAccumulator error_accumulator(&logger);
  ProtectedAudienceInput decoded_protected_audience_input =
      Decode(serialized_cbor, error_accumulator, /*fail_fast=*/false);
  ASSERT_TRUE(error_accumulator.HasErrors());
  VLOG(0) << "Decoded protected audience input:\n"
          << decoded_protected_audience_input.DebugString();

  // Verify all the errors were reported to the error accumulator.
  const auto& client_visible_errors =
      error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE);
  ASSERT_FALSE(client_visible_errors.empty());
  auto observed_client_side_errors_it =
      client_visible_errors.find(ErrorCode::CLIENT_SIDE);
  ASSERT_NE(observed_client_side_errors_it, client_visible_errors.end());
  const auto& observed_client_side_errors =
      observed_client_side_errors_it->second;
  ASSERT_FALSE(observed_client_side_errors.empty());
  std::set<std::string> unexpected_errors;
  absl::c_set_difference(
      observed_client_side_errors, expected_errors,
      std::inserter(unexpected_errors, unexpected_errors.begin()));
  EXPECT_TRUE(unexpected_errors.empty())
      << "Found following unexpected errors were observed:\n"
      << absl::StrJoin(unexpected_errors, "\n");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
