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

#include "absl/log/check.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/compression/gzip.h"
#include "services/common/test/utils/cbor_test_utils.h"
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
inline constexpr int kSampleRecencyMs = 3000;
inline constexpr char kSampleIgOwner[] = "foo_owner";
inline constexpr int kSampleIgIndex = 10;
inline constexpr char kSampleGenerationId[] =
    "6fa459ea-ee8a-3ca4-894e-db77e160355e";
inline constexpr int kSampleRequestMs = 99999;
inline constexpr bool kSampleEnforceKAnon = true;
inline constexpr char kSampleErrorMessage[] = "BadError";
inline constexpr int32_t kSampleErrorCode = 400;
inline constexpr char kTestEvent1[] = "click";
inline constexpr char kTestInteractionUrl1[] = "http://click.com";
inline constexpr char kTestEvent2[] = "scroll";
inline constexpr char kTestInteractionUrl2[] = "http://scroll.com";
inline constexpr char kTestEvent3[] = "close";
inline constexpr char kTestInteractionUrl3[] = "http://close.com";
inline constexpr char kTestReportResultUrl[] = "http://reportResult.com";
inline constexpr char kTestReportWinUrl[] = "http://reportWin.com";
inline constexpr char kConsentedDebugToken[] = "xyz";
inline constexpr char kUsdIso[] = "USD";
inline constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
inline constexpr char kSampleAdRenderUrlHash[] = "adRenderUrlHash";
inline constexpr char kSampleAdRenderUrl[] = "adRenderUrl";
inline constexpr char kSampleAdComponentRenderUrlsHash[] =
    "adComponentRenderUrlHash";
inline constexpr char kSampleAdComponentRenderUrl[] = "adComponentRenderUrl";
inline constexpr char kReportingIdHash[] = "reportingIdHash";
inline constexpr char kSampleBucket[] = "bucket";
inline constexpr int kSampleValue = 21;
inline constexpr float kSampleModifiedBid = 1.23;
inline constexpr char kSampleBidCurrency[] = "bidCurrency";
inline constexpr char kSampleAdMetadata[] = "adMetadata";
inline constexpr char kSampleBuyerAndSellerReportingId[] =
    "buyerAndSellerReportingId";
inline constexpr char kSampleBuyerReportingId[] = "buyerReportingId";
inline constexpr char kSampleSelectableBuyerAndSellerReportingId[] =
    "selectableBuyerAndSellerReportingId";
inline constexpr int kSampleWinnerPositionalIndex = 5;

RequestLogContext log_context{{}, server_common::ConsentedDebugConfiguration()};

using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using InteractionUrlMap = ::google::protobuf::Map<std::string, std::string>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;

struct CborInterestGroupConfig {
  std::optional<int> recency;
  std::optional<int> recency_ms;
};

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

struct cbor_item_t* BuildSampleCborInterestGroup(
    CborInterestGroupConfig cbor_interest_group_config = {}) {
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
      BuildStringArrayMapPair(kAdComponents, {kSampleAdComponentRenderId1,
                                              kSampleAdComponentRenderId2})));
  EXPECT_TRUE(cbor_map_add(
      interest_group,
      BuildStringArrayMapPair(kAds, {kSampleAdRenderId1, kSampleAdRenderId2})));

  cbor_item_t* browser_signals = cbor_new_definite_map(kNumBrowserSignalKeys);
  EXPECT_TRUE(cbor_map_add(browser_signals,
                           BuildIntMapPair(kJoinCount, kSampleJoinCount)));
  EXPECT_TRUE(cbor_map_add(browser_signals,
                           BuildIntMapPair(kBidCount, kSampleBidCount)));
  if (cbor_interest_group_config.recency.has_value()) {
    EXPECT_TRUE(cbor_map_add(
        browser_signals,
        BuildIntMapPair(kRecency, cbor_interest_group_config.recency.value())));
  }
  if (cbor_interest_group_config.recency_ms.has_value()) {
    EXPECT_TRUE(cbor_map_add(
        browser_signals,
        BuildIntMapPair(kRecencyMs,
                        cbor_interest_group_config.recency_ms.value())));
  }
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

ScoreAdsResponse::AdScore MakeARandomComponentAdScore() {
  // Setup a winning bid.
  absl::string_view interest_group = "interest_group";
  absl::string_view ad_render_url = "https://ad-found-here.com/ad-1";
  absl::string_view interest_group_owner = "https://ig-owner.com:1234";
  absl::string_view ad_metadata = "ad_metadata";
  const float modified_bid = 10.21;
  const float buyer_bid = 1.21;
  const float desirability = 2.35;

  ScoreAdsResponse::AdScore winner;
  winner.set_render(ad_render_url);
  winner.set_desirability(desirability);
  winner.set_bid(modified_bid);
  winner.set_bid_currency(kUsdIso);
  winner.set_buyer_bid(buyer_bid);
  winner.set_interest_group_name(interest_group);
  winner.set_interest_group_owner(interest_group_owner);
  winner.set_allow_component_auction(true);
  winner.set_ad_metadata(ad_metadata);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  return winner;
}

void TestUpdateGroupMap(const UpdateGroupMap& decoded_update_groups,
                        const UpdateGroupMap& expected_update_groups) {
  EXPECT_EQ(decoded_update_groups.size(), expected_update_groups.size());
  for (const auto& [key, message] : expected_update_groups) {
    std::string difference;
    google::protobuf::util::MessageDifferencer differencer;
    differencer.ReportDifferencesToString(&difference);
    EXPECT_TRUE(differencer.Compare(decoded_update_groups.at(key), message))
        << difference;
  }
}

void TestDecode(const CborInterestGroupConfig& cbor_interest_group_config) {
  ScopedCbor protected_auction_input(
      cbor_new_definite_map(kNumRequestRootKeys));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           BuildStringMapPair(kPublisher, kSamplePublisher)));
  EXPECT_TRUE(
      cbor_map_add(*protected_auction_input,
                   BuildStringMapPair(kGenerationId, kSampleGenerationId)));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           BuildBoolMapPair(kDebugReporting, true)));
  EXPECT_TRUE(
      cbor_map_add(*protected_auction_input,
                   BuildIntMapPair(kRequestTimestampMs, kSampleRequestMs)));
  EXPECT_TRUE(
      cbor_map_add(*protected_auction_input,
                   BuildBoolMapPair(kEnforceKAnon, kSampleEnforceKAnon)));

  ScopedCbor ig_array(cbor_new_definite_array(1));
  EXPECT_TRUE(cbor_array_push(
      *ig_array, BuildSampleCborInterestGroup(cbor_interest_group_config)));
  cbor_item_t* ig_bytestring = CompressInterestGroups(ig_array);

  cbor_item_t* interest_group_data_map = cbor_new_definite_map(1);
  EXPECT_TRUE(cbor_map_add(interest_group_data_map,
                           {cbor_move(cbor_build_stringn(
                                kSampleIgOwner, sizeof(kSampleIgOwner) - 1)),
                            cbor_move(ig_bytestring)}));
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  cbor_item_t* consented_debug_config_map =
      cbor_new_definite_map(kNumConsentedDebugConfigKeys);
  EXPECT_TRUE(cbor_map_add(consented_debug_config_map,
                           BuildBoolMapPair(kIsConsented, true)));
  EXPECT_TRUE(cbor_map_add(consented_debug_config_map,
                           BuildStringMapPair(kToken, kConsentedDebugToken)));
  EXPECT_TRUE(cbor_map_add(consented_debug_config_map,
                           BuildBoolMapPair(kIsDebugResponse, true)));
  EXPECT_TRUE(cbor_map_add(
      *protected_auction_input,
      {cbor_move(cbor_build_stringn(kConsentedDebugConfig,
                                    sizeof(kConsentedDebugConfig) - 1)),
       cbor_move(consented_debug_config_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_auction_input);

  ErrorAccumulator error_accumulator(&log_context);
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());

  ProtectedAuctionInput expected;
  expected.set_publisher_name(kSamplePublisher);
  expected.set_enable_debug_reporting(true);
  expected.set_generation_id(kSampleGenerationId);
  expected.set_request_timestamp_ms(kSampleRequestMs);
  expected.set_enforce_kanon(kSampleEnforceKAnon);

  BuyerInput::InterestGroup expected_ig;
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
  if (cbor_interest_group_config.recency.has_value()) {
    signals->set_recency(cbor_interest_group_config.recency.value());
  }
  if (cbor_interest_group_config.recency_ms.has_value()) {
    signals->set_recency_ms(cbor_interest_group_config.recency_ms.value());
  }
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

  server_common::ConsentedDebugConfiguration consented_debug_config;
  consented_debug_config.set_is_consented(true);
  consented_debug_config.set_token(kConsentedDebugToken);
  consented_debug_config.set_is_debug_info_in_response(true);
  *expected.mutable_consented_debug_config() =
      std::move(consented_debug_config);

  std::string papi_differences;
  google::protobuf::util::MessageDifferencer papi_differencer;
  papi_differencer.ReportDifferencesToString(&papi_differences);
  // Note that this comparison is fragile because the CBOR encoding depends
  // on the order in which the data was created and if we have a difference
  // between the order in which the data items are added in the test vs how
  // they are added in the implementation, we will start to see failures.
  if (!papi_differencer.Compare(actual, expected)) {
    ABSL_LOG(INFO) << "Actual proto does not match expected proto";
    ABSL_LOG(INFO) << "\nExpected:\n" << expected.DebugString();
    ABSL_LOG(INFO) << "\nActual:\n" << actual.DebugString();
    ABSL_LOG(INFO) << "\nFound differences in ProtectedAuctionInput:\n"
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
    ABSL_LOG(INFO) << "\nExpected BuyerInput:\n"
                   << expected_buyer_input.DebugString();
    BuyerInput actual_buyer_input =
        DecodeBuyerInputs(actual.buyer_input(), error_accumulator)
            .begin()
            ->second;
    ABSL_LOG(INFO) << "\nActual BuyerInput:\n"
                   << actual_buyer_input.DebugString();
    EXPECT_TRUE(
        !bi_differencer.Compare(actual_buyer_input, expected_buyer_input))
        << bi_differences;
    FAIL();
  }
}

TEST(ChromeRequestUtils, DecodeSuccessWithRecencyInBrowserSignals) {
  CborInterestGroupConfig cbor_interest_group_config = {
      .recency = kSampleRecency,
  };
  TestDecode(cbor_interest_group_config);
}

TEST(ChromeRequestUtils, DecodeSuccessWithRecencyMsInBrowserSignals) {
  CborInterestGroupConfig cbor_interest_group_config = {.recency_ms =
                                                            kSampleRecencyMs};
  TestDecode(cbor_interest_group_config);
}

TEST(ChromeRequestUtils, DecodeSuccessWithRecencyAndRecencyMsInBrowserSignals) {
  CborInterestGroupConfig cbor_interest_group_config = {
      .recency = kSampleRecency, .recency_ms = kSampleRecencyMs};
  TestDecode(cbor_interest_group_config);
}

TEST(ChromeRequestUtils, Decode_FailOnWrongType) {
  ScopedCbor root(cbor_build_stringn("string", 6));
  std::string serialized_cbor = SerializeCbor(*root);

  ErrorAccumulator error_accumulator(&log_context);
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());

  const std::string expected_error =
      absl::StrFormat(kInvalidTypeError, kProtectedAuctionInput, kMap, kString);
  EXPECT_TRUE(ContainsClientError(
      error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
      expected_error));
}

TEST(ChromeRequestUtils, Decode_FailOnUnsupportedVersion) {
  ScopedCbor protected_auction_input(cbor_new_definite_map(1));
  EXPECT_TRUE(
      cbor_map_add(*protected_auction_input, BuildIntMapPair(kVersion, 999)));
  std::string serialized_cbor = SerializeCbor(*protected_auction_input);

  ErrorAccumulator error_accumulator(&log_context);
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());

  const std::string expected_error =
      absl::StrFormat(kUnsupportedSchemaVersionError, 999);
  EXPECT_TRUE(ContainsClientError(
      error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
      expected_error));
}

TEST(ChromeRequestUtils, Decode_FailOnMalformedCompresedBytestring) {
  ScopedCbor protected_auction_input(cbor_new_definite_map(1));
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
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_auction_input);

  ErrorAccumulator error_accumulator(&log_context);
  // The main decoding method for protected audience input doesn't decompress
  // and decode the BuyerInput. The latter is handled separately.
  ProtectedAuctionInput actual =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator);
  ASSERT_FALSE(error_accumulator.HasErrors());

  absl::flat_hash_map<absl::string_view, BuyerInput> buyer_inputs =
      DecodeBuyerInputs(actual.buyer_input(), error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());
  EXPECT_TRUE(ContainsClientError(
      error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
      kMalformedCompressedBytestring));
}

TEST(ChromeResponseUtils, VerifyBiddingGroupBuyerOriginOrdering) {
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex interest_group_index;
  interest_group_index.add_index(1);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, interest_group_index);
  bidding_group_map.try_emplace(interest_group_owner_2, interest_group_index);
  bidding_group_map.try_emplace(interest_group_owner_3, interest_group_index);

  // Convert the bidding group map to CBOR.
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumAuctionResultKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializeBiddingGroups(bidding_group_map, err_handler,
                                           *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_origins;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& bidding_group_val = outer_map.begin()->value;
  ASSERT_TRUE(cbor_isa_map(bidding_group_val));
  absl::Span<struct cbor_pair> group_entries(cbor_map_handle(bidding_group_val),
                                             cbor_map_size(bidding_group_val));
  for (const auto& kv : group_entries) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    observed_origins.emplace_back(
        reinterpret_cast<char*>(cbor_string_handle(kv.key)),
        cbor_string_length(kv.key));
  }

  // We expect the shorter length keys to be present first followed. Ties on
  // length are then broken by lexicographic order.
  ASSERT_EQ(observed_origins.size(), 3);
  EXPECT_EQ(observed_origins[0], interest_group_owner_2);
  EXPECT_EQ(observed_origins[1], interest_group_owner_1);
  EXPECT_EQ(observed_origins[2], interest_group_owner_3);
}
void TestReportAndInteractionUrls(const struct cbor_pair reporting_data,
                                  absl::string_view reporting_url_key,
                                  absl::string_view expected_report_url) {
  ASSERT_TRUE(cbor_isa_string(reporting_data.key))
      << "Expected the key to be a string";
  EXPECT_EQ(reporting_url_key, CborDecodeString(reporting_data.key));
  auto& outer_map = reporting_data.value;
  absl::Span<struct cbor_pair> reporting_urls_map(cbor_map_handle(outer_map),
                                                  cbor_map_size(outer_map));
  std::vector<std::string> observed_keys;
  for (const auto& kv : reporting_urls_map) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    observed_keys.emplace_back(CborDecodeString(kv.key));
  }
  EXPECT_EQ(observed_keys[1], kInteractionReportingUrls);
  EXPECT_EQ(observed_keys[0], kReportingUrl);
  EXPECT_EQ(expected_report_url,
            CborDecodeString(reporting_urls_map.at(0).value));
  auto& interaction_map = reporting_urls_map.at(1).value;
  ASSERT_TRUE(cbor_isa_map(interaction_map));
  absl::Span<struct cbor_pair> interaction_urls(
      cbor_map_handle(interaction_map), cbor_map_size(interaction_map));
  ASSERT_EQ(interaction_urls.size(), 3);
  std::vector<std::string> observed_events;
  std::vector<std::string> observed_urls;
  for (const auto& kv : interaction_urls) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    ASSERT_TRUE(cbor_isa_string(kv.value)) << "Expected the key to be a string";
    observed_events.emplace_back(CborDecodeString(kv.key));
    observed_urls.emplace_back(CborDecodeString(kv.value));
  }
  EXPECT_EQ(kTestEvent1, observed_events.at(0));
  EXPECT_EQ(kTestInteractionUrl1, observed_urls.at(0));
  EXPECT_EQ(kTestEvent3, observed_events.at(1));
  EXPECT_EQ(kTestInteractionUrl3, observed_urls.at(1));
  EXPECT_EQ(kTestEvent2, observed_events.at(2));
  EXPECT_EQ(kTestInteractionUrl2, observed_urls.at(2));
}

void TestReportingUrl(const struct cbor_pair reporting_data,
                      absl::string_view reporting_url_key,
                      absl::string_view expected_report_url) {
  ASSERT_TRUE(cbor_isa_string(reporting_data.key))
      << "Expected the key to be a string";
  EXPECT_EQ(reporting_url_key, CborDecodeString(reporting_data.key));
  auto& outer_map = reporting_data.value;
  absl::Span<struct cbor_pair> reporting_urls_map(cbor_map_handle(outer_map),
                                                  cbor_map_size(outer_map));
  std::vector<std::string> observed_keys;
  for (const auto& kv : reporting_urls_map) {
    ASSERT_TRUE(cbor_isa_string(kv.key)) << "Expected the key to be a string";
    observed_keys.emplace_back(CborDecodeString(kv.key));
  }
  EXPECT_EQ(observed_keys[0], kReportingUrl);
  EXPECT_EQ(expected_report_url,
            CborDecodeString(reporting_urls_map.at(0).value));
}

TEST(ChromeResponseUtils, CborSerializeWinReportingUrls) {
  InteractionUrlMap interaction_url_map;
  interaction_url_map.try_emplace(kTestEvent1, kTestInteractionUrl1);
  interaction_url_map.try_emplace(kTestEvent2, kTestInteractionUrl2);
  interaction_url_map.try_emplace(kTestEvent3, kTestInteractionUrl3);
  WinReportingUrls win_reporting_urls;
  win_reporting_urls.mutable_buyer_reporting_urls()->set_reporting_url(
      kTestReportWinUrl);
  win_reporting_urls.mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);
  win_reporting_urls.mutable_component_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  win_reporting_urls.mutable_component_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_component_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_component_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);

  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& report_win_urls_map = outer_map.at(0).value;
  ASSERT_TRUE(cbor_isa_map(report_win_urls_map));
  absl::Span<struct cbor_pair> inner_map(cbor_map_handle(report_win_urls_map),
                                         cbor_map_size(report_win_urls_map));
  ASSERT_EQ(inner_map.size(), 3);
  TestReportAndInteractionUrls(inner_map.at(0), kBuyerReportingUrls,
                               kTestReportWinUrl);
  TestReportAndInteractionUrls(inner_map.at(1), kTopLevelSellerReportingUrls,
                               kTestReportResultUrl);
  TestReportAndInteractionUrls(inner_map.at(2), kComponentSellerReportingUrls,
                               kTestReportResultUrl);
}

TEST(ChromeResponseUtils, NoCborGeneratedWithEmptyWinReportingUrl) {
  WinReportingUrls win_reporting_urls;
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 0);
}

TEST(ChromeResponseUtils, CborWithOnlySellerReprotingUrls) {
  InteractionUrlMap interaction_url_map;
  interaction_url_map.try_emplace(kTestEvent1, kTestInteractionUrl1);
  interaction_url_map.try_emplace(kTestEvent2, kTestInteractionUrl2);
  interaction_url_map.try_emplace(kTestEvent3, kTestInteractionUrl3);
  WinReportingUrls win_reporting_urls;
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent2, kTestInteractionUrl2);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent3, kTestInteractionUrl3);

  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& report_win_urls_map = outer_map.at(0).value;
  ASSERT_TRUE(cbor_isa_map(report_win_urls_map));
  absl::Span<struct cbor_pair> inner_map(cbor_map_handle(report_win_urls_map),
                                         cbor_map_size(report_win_urls_map));
  ASSERT_EQ(inner_map.size(), 1);
  TestReportAndInteractionUrls(inner_map.at(0), kTopLevelSellerReportingUrls,
                               kTestReportResultUrl);
}

TEST(ChromeResponseUtils, CborWithNoInteractionReportingUrls) {
  WinReportingUrls win_reporting_urls;
  win_reporting_urls.mutable_buyer_reporting_urls()->set_reporting_url(
      kTestReportWinUrl);
  win_reporting_urls.mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  ScopedCbor cbor_data_root(cbor_new_definite_map(kNumWinReportingUrlsKeys));
  auto* cbor_internal = cbor_data_root.get();
  auto err_handler = [](const grpc::Status& status) {};
  auto result = CborSerializeWinReportingUrls(win_reporting_urls, err_handler,
                                              *cbor_internal);
  ASSERT_TRUE(result.ok()) << result;

  // Decode the produced CBOR and collect the origins for verification.
  std::vector<std::string> observed_keys;
  absl::Span<struct cbor_pair> outer_map(cbor_map_handle(cbor_internal),
                                         cbor_map_size(cbor_internal));
  ASSERT_EQ(outer_map.size(), 1);
  auto& report_win_urls_map = outer_map.at(0).value;
  ASSERT_TRUE(cbor_isa_map(report_win_urls_map));
  absl::Span<struct cbor_pair> inner_map(cbor_map_handle(report_win_urls_map),
                                         cbor_map_size(report_win_urls_map));
  ASSERT_EQ(inner_map.size(), 2);
  TestReportingUrl(inner_map.at(0), kBuyerReportingUrls, kTestReportWinUrl);
  TestReportingUrl(inner_map.at(1), kTopLevelSellerReportingUrls,
                   kTestReportResultUrl);
}

TEST(ChromeResponseUtils, VerifyCborEncodingWithWinReportingUrls) {
  // Setup a winning bid.
  const std::string interest_group = "interest_group";
  const std::string ad_render_url = "https://ad-found-here.com/ad-1";
  const std::string interest_group_owner = "https://ig-owner.com:1234";
  const int ig_index = 2;
  const float buyer_bid = 10.21;
  const float desirability = 2.35;
  UpdateGroupMap update_group_map;
  ScoreAdsResponse::AdScore winner;
  winner.set_render(ad_render_url);
  winner.set_desirability(desirability);
  winner.set_buyer_bid(buyer_bid);
  winner.set_interest_group_name(interest_group);
  winner.set_interest_group_owner(interest_group_owner);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_component_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.set_buyer_reporting_id(kTestBuyerReportingId);
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(ig_index);
  bidding_group_map.try_emplace(interest_group_owner, std::move(ig_indices));

  auto response_with_cbor =
      Encode(winner, bidding_group_map, update_group_map,
             /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  ABSL_LOG(INFO) << "Encoded CBOR: "
                 << absl::BytesToHexString(*response_with_cbor);
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
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
  EXPECT_TRUE(AreFloatsEqual(decoded_result->bid(), buyer_bid))
      << " Actual: " << decoded_result->bid() << ", Expected: " << buyer_bid;
  EXPECT_TRUE(AreFloatsEqual(decoded_result->score(), desirability))
      << " Actual: " << decoded_result->score()
      << ", Expected: " << desirability;
  EXPECT_EQ(decoded_result->interest_group_name(), interest_group);
  EXPECT_EQ(decoded_result->interest_group_owner(), interest_group_owner);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .buyer_reporting_urls()
                .reporting_url(),
            kTestReportWinUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
  EXPECT_EQ(decoded_result->buyer_reporting_id(), kTestBuyerReportingId);
}

TEST(ChromeResponseUtils, VerifyCborEncoding) {
  // Setup a winning bid.
  const std::string interest_group = "interest_group";
  const std::string ad_render_url = "https://ad-found-here.com/ad-1";
  const std::string interest_group_owner = "https://ig-owner.com:1234";
  const int ig_index = 2;
  const float buyer_bid = 10.21;
  const float desirability = 2.35;
  const int update_if_older_than_ms = 500000;
  ScoreAdsResponse::AdScore winner;
  winner.set_render(ad_render_url);
  winner.set_desirability(desirability);
  winner.set_buyer_bid(buyer_bid);
  winner.set_interest_group_name(interest_group);
  winner.set_interest_group_owner(interest_group_owner);
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(ig_index);
  bidding_group_map.try_emplace(interest_group_owner, std::move(ig_indices));
  // Setup an update group map.
  UpdateGroupMap update_group_map;
  UpdateInterestGroupList update_list;
  UpdateInterestGroup update_first;
  update_first.set_index(0);
  update_first.set_update_if_older_than_ms(update_if_older_than_ms);
  UpdateInterestGroup update_second;
  update_second.set_index(1);
  update_second.set_update_if_older_than_ms(update_if_older_than_ms);
  *update_list.mutable_interest_groups()->Add() = std::move(update_first);
  *update_list.mutable_interest_groups()->Add() = std::move(update_second);
  update_group_map.emplace(interest_group_owner, std::move(update_list));

  auto response_with_cbor =
      Encode(winner, bidding_group_map, update_group_map,
             /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  ABSL_LOG(INFO) << "Encoded CBOR: "
                 << absl::BytesToHexString(*response_with_cbor);
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  // Verify that the decoded result has all the bidding groups correctly set.
  EXPECT_EQ(decoded_result->bidding_groups().size(), 1);
  const auto& [observed_owner, observed_ig_indices] =
      *decoded_result->bidding_groups().begin();
  EXPECT_EQ(observed_owner, interest_group_owner);
  EXPECT_EQ(observed_ig_indices.index_size(), 1);
  EXPECT_EQ(observed_ig_indices.index(0), ig_index);

  TestUpdateGroupMap(decoded_result->update_groups(), update_group_map);

  // Verify that the decoded result has the winning ad correctly set.
  EXPECT_EQ(decoded_result->ad_render_url(), ad_render_url);
  EXPECT_TRUE(AreFloatsEqual(decoded_result->bid(), buyer_bid))
      << " Actual: " << decoded_result->bid() << ", Expected: " << buyer_bid;
  EXPECT_TRUE(AreFloatsEqual(decoded_result->score(), desirability))
      << " Actual: " << decoded_result->score()
      << ", Expected: " << desirability;
  EXPECT_EQ(decoded_result->interest_group_name(), interest_group);
  EXPECT_EQ(decoded_result->interest_group_owner(), interest_group_owner);
  EXPECT_TRUE(decoded_result->win_reporting_urls()
                  .buyer_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(decoded_result->win_reporting_urls()
                  .buyer_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
  EXPECT_TRUE(decoded_result->win_reporting_urls()
                  .top_level_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(decoded_result->win_reporting_urls()
                  .top_level_seller_reporting_urls()
                  .interaction_reporting_urls()
                  .empty());
  EXPECT_TRUE(decoded_result->win_reporting_urls()
                  .component_seller_reporting_urls()
                  .reporting_url()
                  .empty());
  EXPECT_TRUE(decoded_result->win_reporting_urls()
                  .component_seller_reporting_urls()
                  .reporting_url()
                  .empty());
}

TEST(ChromeResponseUtils, CborEncodesComponentAuctionResult) {
  absl::string_view top_level_seller = "https://top-level-seller.com:1234";
  const int ig_index = 2;
  ScoreAdsResponse_AdScore winner = MakeARandomComponentAdScore();
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(ig_index);
  bidding_group_map.try_emplace(winner.interest_group_owner(),
                                std::move(ig_indices));
  UpdateGroupMap update_group_map;
  UpdateInterestGroupList update_list;
  UpdateInterestGroup update_first;
  update_first.set_index(0);
  update_first.set_update_if_older_than_ms(1);
  UpdateInterestGroup update_second;
  update_second.set_index(1);
  update_second.set_update_if_older_than_ms(2);
  *update_list.mutable_interest_groups()->Add() = std::move(update_first);
  *update_list.mutable_interest_groups()->Add() = std::move(update_second);
  update_group_map.emplace("test", std::move(update_list));

  auto response_with_cbor = EncodeComponent(
      top_level_seller, winner, bidding_group_map, update_group_map,
      /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();
  // Verify that the decoded result has all the bidding groups correctly set.
  EXPECT_EQ(decoded_result->bidding_groups().size(), 1);
  const auto& [observed_owner, observed_ig_indices] =
      *decoded_result->bidding_groups().begin();
  EXPECT_EQ(observed_owner, winner.interest_group_owner());
  EXPECT_EQ(observed_ig_indices.index_size(), 1);
  EXPECT_EQ(observed_ig_indices.index(0), ig_index);

  TestUpdateGroupMap(decoded_result->update_groups(), update_group_map);

  // Verify that the decoded result has the winning ad correctly set.
  EXPECT_EQ(decoded_result->ad_render_url(), winner.render());
  EXPECT_EQ(decoded_result->top_level_seller(), top_level_seller);
  EXPECT_EQ(decoded_result->ad_metadata(), winner.ad_metadata());
  EXPECT_TRUE(AreFloatsEqual(decoded_result->bid(), winner.bid()))
      << " Actual: " << decoded_result->bid() << ", Expected: " << winner.bid();
  EXPECT_TRUE(AreFloatsEqual(decoded_result->score(), winner.desirability()))
      << " Actual: " << decoded_result->score()
      << ", Expected: " << winner.desirability();
  EXPECT_EQ(decoded_result->interest_group_name(),
            winner.interest_group_name());
  EXPECT_EQ(decoded_result->interest_group_owner(),
            winner.interest_group_owner());
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .buyer_reporting_urls()
                .reporting_url(),
            kTestReportWinUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .buyer_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .top_level_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .top_level_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .component_seller_reporting_urls()
                .reporting_url(),
            kTestReportResultUrl);
  EXPECT_EQ(decoded_result->win_reporting_urls()
                .component_seller_reporting_urls()
                .interaction_reporting_urls()
                .at(kTestEvent1),
            kTestInteractionUrl1);
}

TEST(ChromeResponseUtils, VerifyCBOREncodedError) {
  ScoreAdsResponse::AdScore winner;
  // Setup a bidding group map.
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  UpdateGroupMap update_group_map;

  AuctionResult::InterestGroupIndex ig_indices;

  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);
  auto response_with_cbor = Encode(winner, bidding_group_map, update_group_map,
                                   error, [](const grpc::Status& status) {});

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();
  EXPECT_EQ(decoded_result->error().message(), kSampleErrorMessage);
  EXPECT_EQ(decoded_result->error().code(), kSampleErrorCode);
}

AuctionResult::KAnonJoinCandidate SampleKAnonJoinCandidate() {
  AuctionResult::KAnonJoinCandidate kanon_winner_join_candidates;
  kanon_winner_join_candidates.set_ad_render_url_hash(kSampleAdRenderUrlHash);
  auto* ad_component_render_urls_hash =
      kanon_winner_join_candidates.mutable_ad_component_render_urls_hash();
  *ad_component_render_urls_hash->Add() = kSampleAdComponentRenderUrlsHash;
  kanon_winner_join_candidates.set_reporting_id_hash(kReportingIdHash);
  return kanon_winner_join_candidates;
}

KAnonAuctionResultData SampleKAnonAuctionResultData() {
  // Setup k-anon ghost winners.
  AuctionResult::KAnonGhostWinner kanon_ghost_winner;
  *kanon_ghost_winner.mutable_k_anon_join_candidates() =
      SampleKAnonJoinCandidate();
  kanon_ghost_winner.set_interest_group_index(kSampleIgIndex);
  kanon_ghost_winner.set_owner(kSampleIgOwner);
  // Add private aggregation signals.
  auto* ghost_winner_private_aggregation_signals =
      kanon_ghost_winner.mutable_ghost_winner_private_aggregation_signals();
  ghost_winner_private_aggregation_signals->set_bucket(kSampleBucket);
  ghost_winner_private_aggregation_signals->set_value(kSampleValue);
  // Add ghost winner for top level auction.
  auto* ghost_winner_for_top_level_auction =
      kanon_ghost_winner.mutable_ghost_winner_for_top_level_auction();
  ghost_winner_for_top_level_auction->set_ad_render_url(kSampleAdRenderUrl);
  auto* ad_component_render_urls =
      ghost_winner_for_top_level_auction->mutable_ad_component_render_urls();
  *ad_component_render_urls->Add() = kSampleAdComponentRenderUrl;
  ghost_winner_for_top_level_auction->set_modified_bid(kSampleModifiedBid);
  ghost_winner_for_top_level_auction->set_bid_currency(kSampleBidCurrency);
  ghost_winner_for_top_level_auction->set_ad_metadata(kSampleAdMetadata);
  ghost_winner_for_top_level_auction->set_buyer_and_seller_reporting_id(
      kSampleBuyerAndSellerReportingId);
  ghost_winner_for_top_level_auction->set_buyer_reporting_id(
      kSampleBuyerReportingId);
  ghost_winner_for_top_level_auction
      ->set_selectable_buyer_and_seller_reporting_id(
          kSampleSelectableBuyerAndSellerReportingId);

  return {.kanon_ghost_winners = {std::move(kanon_ghost_winner)},
          .kanon_winner_join_candidates = SampleKAnonJoinCandidate(),
          .kanon_winner_positional_index = kSampleWinnerPositionalIndex};
}

TEST(ChromeResponseUtils, EncodesKAnonData) {
  KAnonAuctionResultData kanon_auction_result_data =
      SampleKAnonAuctionResultData();

  ScoreAdsResponse::AdScore winner;
  auto response_with_cbor = Encode(
      winner, /*bidding_group_map=*/{}, /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      &kanon_auction_result_data);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->k_anon_winner_positional_index(),
            kSampleWinnerPositionalIndex);
  // Validate winner join candidate.
  ASSERT_TRUE(decoded_result->has_k_anon_winner_join_candidates());
  const auto& k_anon_winner_join_candidates =
      decoded_result->k_anon_winner_join_candidates();
  EXPECT_EQ(k_anon_winner_join_candidates.ad_render_url_hash(),
            kSampleAdRenderUrlHash);
  EXPECT_EQ(k_anon_winner_join_candidates.reporting_id_hash(),
            kReportingIdHash);
  const auto& ad_component_render_urls_hash =
      k_anon_winner_join_candidates.ad_component_render_urls_hash();
  ASSERT_EQ(ad_component_render_urls_hash.size(), 1);
  EXPECT_EQ(ad_component_render_urls_hash[0], kSampleAdComponentRenderUrlsHash);

  // Validate ghost winner candidates.
  ASSERT_EQ(decoded_result->k_anon_ghost_winners_size(), 1);
  const auto& ghost_winner = decoded_result->k_anon_ghost_winners(0);
  EXPECT_EQ(ghost_winner.interest_group_index(), kSampleIgIndex);
  EXPECT_EQ(ghost_winner.owner(), kSampleIgOwner);
  const auto& ghost_join_candidate = ghost_winner.k_anon_join_candidates();
  EXPECT_EQ(ghost_join_candidate.ad_render_url_hash(), kSampleAdRenderUrlHash);
  ASSERT_EQ(ghost_join_candidate.ad_component_render_urls_hash_size(), 1);
  EXPECT_EQ(ghost_join_candidate.ad_component_render_urls_hash(0),
            kSampleAdComponentRenderUrlsHash);
  EXPECT_EQ(ghost_join_candidate.reporting_id_hash(), kReportingIdHash);
  // Validate private aggregation signals.
  const auto& observed_ghost_winner_private_aggregation_signals =
      ghost_winner.ghost_winner_private_aggregation_signals();
  EXPECT_EQ(observed_ghost_winner_private_aggregation_signals.bucket(),
            kSampleBucket);
  EXPECT_EQ(observed_ghost_winner_private_aggregation_signals.value(),
            kSampleValue);
  // Validate ghost winner for top level auction.
  const auto& observed_ghost_winner_for_top_level_auction =
      ghost_winner.ghost_winner_for_top_level_auction();
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction.ad_render_url(),
            kSampleAdRenderUrl);
  ASSERT_EQ(observed_ghost_winner_for_top_level_auction
                .ad_component_render_urls_size(),
            1);
  EXPECT_EQ(
      observed_ghost_winner_for_top_level_auction.ad_component_render_urls(0),
      kSampleAdComponentRenderUrl);
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction.modified_bid(),
            kSampleModifiedBid);
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction.bid_currency(),
            kSampleBidCurrency);
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction.ad_metadata(),
            kSampleAdMetadata);
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction
                .buyer_and_seller_reporting_id(),
            kSampleBuyerAndSellerReportingId);
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction.buyer_reporting_id(),
            kSampleBuyerReportingId);
  EXPECT_EQ(observed_ghost_winner_for_top_level_auction
                .selectable_buyer_and_seller_reporting_id(),
            kSampleSelectableBuyerAndSellerReportingId);
}

std::string ErrStr(absl::string_view field_name,
                   absl::string_view expected_type,
                   absl::string_view observed_type) {
  return absl::StrFormat(kInvalidTypeError, field_name, expected_type,
                         observed_type);
}

TEST(WebRequestUtils, Decode_FailsAndGetsAllErrors) {
  ScopedCbor protected_auction_input(
      cbor_new_definite_map(kNumRequestRootKeys));

  std::set<std::string> expected_errors;

  // Malformed key type for publisher.
  cbor_pair publisher_kv = {
      cbor_move(cbor_build_bytestring(reinterpret_cast<cbor_data>(kPublisher),
                                      sizeof(kPublisher) - 1)),
      cbor_move(
          cbor_build_stringn(kSamplePublisher, sizeof(kSamplePublisher) - 1))};
  EXPECT_TRUE(cbor_map_add(*protected_auction_input, publisher_kv));
  expected_errors.emplace(ErrStr(/*field_name=*/kRootCborKey,
                                 /*expected_type=*/kCborTypeString,
                                 /*observed_type=*/kCborTypeByteString));

  // Malformed value type for generation id.
  cbor_pair gen_id_kv = {
      cbor_move(cbor_build_stringn(kGenerationId, sizeof(kGenerationId) - 1)),
      cbor_move(cbor_build_bytestring(
          reinterpret_cast<cbor_data>(kSampleGenerationId),
          sizeof(kSampleGenerationId) - 1))};
  EXPECT_TRUE(cbor_map_add(*protected_auction_input, gen_id_kv));
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
  EXPECT_TRUE(cbor_map_add(interest_group, ig_name_kv));
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
  EXPECT_TRUE(cbor_map_add(interest_group, browser_signals_kv));
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
  EXPECT_TRUE(cbor_map_add(interest_group, bidding_signals_kv));
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
  EXPECT_TRUE(cbor_map_add(*protected_auction_input,
                           {cbor_move(cbor_build_stringn(
                                kInterestGroups, sizeof(kInterestGroups) - 1)),
                            cbor_move(interest_group_data_map)}));

  std::string serialized_cbor = SerializeCbor(*protected_auction_input);

  ErrorAccumulator error_accumulator(&log_context);
  ProtectedAuctionInput decoded_protected_auction_input =
      Decode<ProtectedAuctionInput>(serialized_cbor, error_accumulator,
                                    /*fail_fast=*/false);
  ASSERT_TRUE(error_accumulator.HasErrors());
  ABSL_LOG(INFO) << "Decoded protected audience input:\n"
                 << decoded_protected_auction_input.DebugString();

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

absl::StatusOr<std::string> SerializeToCbor(cbor_item_t* cbor_data_root) {
  // Serialize the payload to CBOR.
  const size_t cbor_serialized_data_size = cbor_serialized_size(cbor_data_root);
  if (!cbor_serialized_data_size) {
    return absl::InternalError("Unable to serialize (data too large)");
  }

  std::vector<unsigned char> byte_string(cbor_serialized_data_size);
  if (cbor_serialize(cbor_data_root, byte_string.data(),
                     cbor_serialized_data_size) == 0) {
    return absl::InternalError("Failed to serialize to CBOR");
  }
  std::string out;
  for (const auto& val : byte_string) {
    out.append(absl::StrCat(absl::Hex(val, absl::kZeroPad2)));
  }
  return out;
}

TEST(ChromeResponseUtils, UintsAreCompactlyCborEncoded) {
  ScopedCbor single_byte_cbor(cbor_build_uint(23));
  auto single_byte = SerializeToCbor(*single_byte_cbor);
  ASSERT_TRUE(single_byte.ok()) << single_byte.status();
  EXPECT_EQ(*single_byte, "17");

  ScopedCbor two_bytes_cbor(cbor_build_uint(255));
  auto two_bytes = SerializeToCbor(*two_bytes_cbor);
  ASSERT_TRUE(two_bytes.ok()) << two_bytes.status();
  EXPECT_EQ(*two_bytes, "18ff");

  ScopedCbor three_bytes_cbor(cbor_build_uint(65535));
  auto three_bytes = SerializeToCbor(*three_bytes_cbor);
  ASSERT_TRUE(three_bytes.ok()) << three_bytes.status();
  EXPECT_EQ(*three_bytes, "19ffff");

  ScopedCbor five_bytes_cbor(cbor_build_uint(4294967295));
  auto five_bytes = SerializeToCbor(*five_bytes_cbor);
  ASSERT_TRUE(five_bytes.ok()) << five_bytes.status();
  EXPECT_EQ(*five_bytes, "1affffffff");
}

TEST(ChromeResponseUtils, NegIntsAreCompactlyCborEncoded) {
  ScopedCbor single_byte_cbor(cbor_build_int(-23));
  auto single_byte = SerializeToCbor(*single_byte_cbor);
  ASSERT_TRUE(single_byte.ok()) << single_byte.status();
  EXPECT_EQ(*single_byte, "36");

  ScopedCbor two_bytes_cbor(cbor_build_int(-127));
  auto two_bytes = SerializeToCbor(*two_bytes_cbor);
  ASSERT_TRUE(two_bytes.ok()) << two_bytes.status();
  EXPECT_EQ(*two_bytes, "387e");

  ScopedCbor five_bytes_cbor(cbor_build_int(-2147483648));
  auto five_bytes = SerializeToCbor(*five_bytes_cbor);
  ASSERT_TRUE(five_bytes.ok()) << five_bytes.status();
  EXPECT_EQ(*five_bytes, "3a7fffffff");
}

TEST(ChromeResponseUtils, FloatsAreCompactlyCborEncoded) {
  auto half_precision_cbor = cbor_build_float(0.0);
  ASSERT_TRUE(half_precision_cbor.ok()) << half_precision_cbor.status();
  auto half_precision = SerializeToCbor(*half_precision_cbor);
  cbor_decref(&*half_precision_cbor);
  ASSERT_TRUE(half_precision.ok()) << half_precision.status();
  EXPECT_EQ(*half_precision, "f90000");

  auto single_precision_cbor = cbor_build_float(std::pow(2.0, -24));
  ASSERT_TRUE(single_precision_cbor.ok()) << single_precision_cbor.status();
  auto single_precision = SerializeToCbor(*single_precision_cbor);
  cbor_decref(&*single_precision_cbor);
  ASSERT_TRUE(single_precision.ok()) << single_precision.status();
  EXPECT_EQ(*single_precision, "f90001");

  auto fine_precision_cbor = cbor_build_float(std::pow(2.0, -32));
  ASSERT_TRUE(fine_precision_cbor.ok()) << fine_precision_cbor.status();
  auto fine_precision = SerializeToCbor(*fine_precision_cbor);
  cbor_decref(&*fine_precision_cbor);
  EXPECT_EQ(*fine_precision, "fa2f800000");

  auto double_precision_cbor = cbor_build_float(0.16);
  ASSERT_TRUE(double_precision_cbor.ok()) << double_precision_cbor.status();
  auto double_precision = SerializeToCbor(*double_precision_cbor);
  cbor_decref(&*double_precision_cbor);
  ASSERT_TRUE(double_precision.ok()) << double_precision.status();
  EXPECT_EQ(*double_precision, "fb3fc47ae147ae147b");

  auto rand_cbor_1 = cbor_build_float(10.21);
  ASSERT_TRUE(rand_cbor_1.ok()) << rand_cbor_1.status();
  auto rand_1 = SerializeToCbor(*rand_cbor_1);
  cbor_decref(&*rand_cbor_1);
  ASSERT_TRUE(rand_1.ok()) << rand_1.status();
  EXPECT_EQ(*rand_1, "fb40246b851eb851ec");

  auto rand_cbor_2 = cbor_build_float(2.35);
  ASSERT_TRUE(rand_cbor_2.ok()) << rand_cbor_2.status();
  auto rand_2 = SerializeToCbor(*rand_cbor_2);
  cbor_decref(&*rand_cbor_2);
  ASSERT_TRUE(rand_2.ok()) << rand_2.status();
  EXPECT_EQ(*rand_2, "fb4002cccccccccccd");
}

TEST(ChromeResponseUtils, VerifyMinimalResponseEncoding) {
  ScoreAdsResponse::AdScore winner;
  winner.set_interest_group_owner("https://adtech.com");
  winner.set_interest_group_name("ig1");
  winner.set_desirability(156671.781);
  // Should accomplish nothing as bid should not be included in the response for
  // non-component auction.
  winner.set_buyer_bid(0.195839122);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex indices;
  indices.add_index(7);
  indices.add_index(2);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, indices);
  bidding_group_map.try_emplace(interest_group_owner_2, indices);
  bidding_group_map.try_emplace(interest_group_owner_3, indices);
  bidding_group_map.try_emplace("owner1", std::move(indices));

  auto kanon_auction_result_data = SampleKAnonAuctionResultData();

  UpdateGroupMap update_group_map;
  UpdateInterestGroupList update_list;
  UpdateInterestGroup update_interest_group;
  update_interest_group.set_index(1);
  update_interest_group.set_update_if_older_than_ms(1000);
  *update_list.mutable_interest_groups()->Add() =
      std::move(update_interest_group);
  update_group_map.emplace(interest_group_owner_1, std::move(update_list));

  auto ret = Encode(
      std::move(winner), bidding_group_map, update_group_map, std::nullopt,
      [](auto error) {}, &kanon_auction_result_data);

  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ad63626964fa3e488a0d6573636f7265fa4818fff26769734368616666f46a636f6d"
          "706f6e656e7473806b616452656e64657255524c606c75706461746547726f757073"
          "a16369673181a265696e646578017375706461746549664f6c6465725468616e4d73"
          "1903e86d62696464696e6747726f757073a4627a6982070263696731820702636968"
          "31820702666f776e6572318207027077696e5265706f7274696e6755524c73a27262"
          "757965725265706f7274696e6755524c73a26c7265706f7274696e6755524c746874"
          "74703a2f2f7265706f727457696e2e636f6d7818696e746572616374696f6e526570"
          "6f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e636f6d"
          "781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a26c726570"
          "6f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e636f6d78"
          "18696e746572616374696f6e5265706f7274696e6755524c73a165636c69636b7068"
          "7474703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e616d6563"
          "696731716b416e6f6e47686f737457696e6e65727381a5656f776e657269666f6f5f"
          "6f776e657272696e74657265737447726f7570496e6465780a736b416e6f6e4a6f69"
          "6e43616e64696461746573a36f616452656e64657255524c486173686f616452656e"
          "64657255726c486173686f7265706f7274696e674964486173686f7265706f727469"
          "6e6749644861736878196164436f6d706f6e656e7452656e64657255524c73486173"
          "688178186164436f6d706f6e656e7452656e64657255726c48617368781d67686f73"
          "7457696e6e6572466f72546f704c6576656c41756374696f6ea86a61644d65746164"
          "6174616a61644d657461646174616b616452656e64657255524c6b616452656e6465"
          "7255726c6b62696443757272656e63796b62696443757272656e63796b6d6f646966"
          "696564426964fa3f9d70a47062757965725265706f7274696e674964706275796572"
          "5265706f7274696e674964756164436f6d706f6e656e7452656e64657255524c7381"
          "746164436f6d706f6e656e7452656e64657255726c78196275796572416e6453656c"
          "6c65725265706f7274696e67496478196275796572416e6453656c6c65725265706f"
          "7274696e674964782373656c65637461626c654275796572416e6453656c6c657252"
          "65706f7274696e674964782373656c65637461626c654275796572416e6453656c6c"
          "65725265706f7274696e674964782467686f737457696e6e65725072697661746541"
          "67677265676174696f6e5369676e616c73a26576616c756515666275636b65746662"
          "75636b657472696e74657265737447726f75704f776e65727268747470733a2f2f61"
          "64746563682e636f6d78196b416e6f6e57696e6e65724a6f696e43616e6469646174"
          "6573a36f616452656e64657255524c486173686f616452656e64657255726c486173"
          "686f7265706f7274696e674964486173686f7265706f7274696e6749644861736878"
          "196164436f6d706f6e656e7452656e64657255524c73486173688178186164436f6d"
          "706f6e656e7452656e64657255726c48617368781a6b416e6f6e57696e6e6572506f"
          "736974696f6e616c496e64657805"));
}

TEST(ChromeResponseUtils, VerifyMinimalComponentResponseEncoding) {
  ScoreAdsResponse::AdScore winner;
  winner.set_interest_group_owner("https://adtech.com");
  winner.set_interest_group_name("ig1");
  winner.set_desirability(156671.781);
  winner.set_buyer_bid(0.6666);
  winner.set_bid(0.195839122);
  winner.set_bid_currency(kUsdIso);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.set_ad_metadata("sampleAdMetadata");
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex indices;
  indices.add_index(7);
  indices.add_index(2);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, indices);
  bidding_group_map.try_emplace(interest_group_owner_2, indices);
  bidding_group_map.try_emplace(interest_group_owner_3, indices);
  bidding_group_map.try_emplace("owner1", std::move(indices));

  UpdateGroupMap update_group_map;
  UpdateInterestGroupList update_list;
  UpdateInterestGroup update_interest_group;
  update_interest_group.set_index(1);
  update_interest_group.set_update_if_older_than_ms(1000);
  *update_list.mutable_interest_groups()->Add() =
      std::move(update_interest_group);
  update_group_map.emplace(interest_group_owner_2, std::move(update_list));

  auto ret = EncodeComponent("https://top-level-adtech.com", std::move(winner),
                             bidding_group_map, update_group_map, std::nullopt,
                             [](auto error) {});
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ad63626964fa3e488a0d6573636f7265fa4818fff26769734368616666f46a61644d"
          "657461646174617073616d706c6541644d657461646174616a636f6d706f6e656e74"
          "73806b616452656e64657255524c606b62696443757272656e6379635553446c7570"
          "6461746547726f757073a1627a6981a265696e646578017375706461746549664f6c"
          "6465725468616e4d731903e86d62696464696e6747726f757073a4627a6982070263"
          "69673182070263696831820702666f776e6572318207026e746f704c6576656c5365"
          "6c6c6572781c68747470733a2f2f746f702d6c6576656c2d6164746563682e636f6d"
          "7077696e5265706f7274696e6755524c73a27262757965725265706f7274696e6755"
          "524c73a26c7265706f7274696e6755524c74687474703a2f2f7265706f727457696e"
          "2e636f6d7818696e746572616374696f6e5265706f7274696e6755524c73a165636c"
          "69636b70687474703a2f2f636c69636b2e636f6d781b746f704c6576656c53656c6c"
          "65725265706f7274696e6755524c73a26c7265706f7274696e6755524c7768747470"
          "3a2f2f7265706f7274526573756c742e636f6d7818696e746572616374696f6e5265"
          "706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e636f"
          "6d71696e74657265737447726f75704e616d656369673172696e7465726573744772"
          "6f75704f776e65727268747470733a2f2f6164746563682e636f6d"));
}

TEST(ChromeResponseUtils, VerifyMinimalComponentResponseEncodingNoBidCurrency) {
  ScoreAdsResponse::AdScore winner;
  winner.set_interest_group_owner("https://adtech.com");
  winner.set_interest_group_name("ig1");
  winner.set_desirability(156671.781);
  winner.set_buyer_bid(0.6666);
  winner.set_bid(0.195839122);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.set_ad_metadata("sampleAdMetadata");
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex indices;
  indices.add_index(7);
  indices.add_index(2);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, indices);
  bidding_group_map.try_emplace(interest_group_owner_2, indices);
  bidding_group_map.try_emplace(interest_group_owner_3, indices);
  bidding_group_map.try_emplace("owner1", std::move(indices));

  UpdateGroupMap update_group_map;

  auto ret = EncodeComponent("https://top-level-adtech.com", std::move(winner),
                             bidding_group_map, update_group_map, std::nullopt,
                             [](auto error) {});
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ac63626964fa3e488a0d6573636f7265fa4818fff26769734368616666f46a61644d"
          "657461646174617073616d706c6541644d657461646174616a636f6d706f6e656e74"
          "73806b616452656e64657255524c606c75706461746547726f757073a06d62696464"
          "696e6747726f757073a4627a698207026369673182070263696831820702666f776e"
          "6572318207026e746f704c6576656c53656c6c6572781c68747470733a2f2f746f70"
          "2d6c6576656c2d6164746563682e636f6d7077696e5265706f7274696e6755524c73"
          "a27262757965725265706f7274696e6755524c73a26c7265706f7274696e6755524c"
          "74687474703a2f2f7265706f727457696e2e636f6d7818696e746572616374696f6e"
          "5265706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e"
          "636f6d781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a26c"
          "7265706f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e63"
          "6f6d7818696e746572616374696f6e5265706f7274696e6755524c73a165636c6963"
          "6b70687474703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e61"
          "6d656369673172696e74657265737447726f75704f776e65727268747470733a2f2f"
          "6164746563682e636f6d"));
}

TEST(ChromeResponseUtils, VerifyMinimalEncodingWithBuyerReportingId) {
  ScoreAdsResponse::AdScore winner;
  winner.set_interest_group_owner("https://adtech.com");
  winner.set_interest_group_name("ig1");
  winner.set_desirability(156671.781);
  winner.set_buyer_bid(0.195839122);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->set_reporting_url(kTestReportWinUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_buyer_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->set_reporting_url(kTestReportResultUrl);
  winner.mutable_win_reporting_urls()
      ->mutable_top_level_seller_reporting_urls()
      ->mutable_interaction_reporting_urls()
      ->try_emplace(kTestEvent1, kTestInteractionUrl1);
  winner.set_buyer_reporting_id(kTestBuyerReportingId);
  const std::string interest_group_owner_1 = "ig1";
  const std::string interest_group_owner_2 = "zi";
  const std::string interest_group_owner_3 = "ih1";
  AuctionResult::InterestGroupIndex indices;
  indices.add_index(7);
  indices.add_index(2);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(interest_group_owner_1, indices);
  bidding_group_map.try_emplace(interest_group_owner_2, indices);
  bidding_group_map.try_emplace(interest_group_owner_3, indices);
  bidding_group_map.try_emplace("owner1", std::move(indices));

  UpdateGroupMap update_group_map;

  auto ret = Encode(std::move(winner), bidding_group_map, update_group_map,
                    std::nullopt, [](auto error) {});
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ab63626964fa3e488a0d6573636f7265fa4818fff26769734368616666f46a636f6d"
          "706f6e656e7473806b616452656e64657255524c606c75706461746547726f757073"
          "a06d62696464696e6747726f757073a4627a69820702636967318207026369683182"
          "0702666f776e6572318207027062757965725265706f7274696e6749647474657374"
          "42757965725265706f7274696e6749647077696e5265706f7274696e6755524c73a2"
          "7262757965725265706f7274696e6755524c73a26c7265706f7274696e6755524c74"
          "687474703a2f2f7265706f727457696e2e636f6d7818696e746572616374696f6e52"
          "65706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e63"
          "6f6d781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a26c72"
          "65706f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e636f"
          "6d7818696e746572616374696f6e5265706f7274696e6755524c73a165636c69636b"
          "70687474703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e616d"
          "656369673172696e74657265737447726f75704f776e65727268747470733a2f2f61"
          "64746563682e636f6d"));
}

MATCHER_P(ProtoEq, value, "") {
  AuctionResult result;
  CHECK(google::protobuf::TextFormat::ParseFromString(value, &result));
  return google::protobuf::util::MessageDifferencer::Equals(arg, result);
}

TEST(CborDecodeAuctionResultToProto, ValidAuctionResult) {
  absl::string_view hex_cbor =
      "AA63626964FA41235C296573636F7265FA401666666769734368616666F46B616452656E"
      "64657255524C781E68747470733A2F2F61642D666F756E642D686572652E636F6D2F6164"
      "2D316B62696443757272656E6379635553446C75706461746547726F757073A174687474"
      "70733A2F2F69672D6F776E65722E636F6D81A265696E646578017375706461746549664F"
      "6C6465725468616E4D731903E86D62696464696E6747726F757073A1781968747470733A"
      "2F2F69672D6F776E65722E636F6D3A3132333481027077696E5265706F7274696E675552"
      "4C73A37262757965725265706F7274696E6755524C73A26C7265706F7274696E6755524C"
      "74687474703A2F2F7265706F727457696E2E636F6D7818696E746572616374696F6E5265"
      "706F7274696E6755524C73A165636C69636B70687474703A2F2F636C69636B2E636F6D78"
      "1B746F704C6576656C53656C6C65725265706F7274696E6755524C73A26C7265706F7274"
      "696E6755524C77687474703A2F2F7265706F7274526573756C742E636F6D7818696E7465"
      "72616374696F6E5265706F7274696E6755524C73A165636C69636B70687474703A2F2F63"
      "6C69636B2E636F6D781C636F6D706F6E656E7453656C6C65725265706F7274696E675552"
      "4C73A26C7265706F7274696E6755524C77687474703A2F2F7265706F7274526573756C74"
      "2E636F6D7818696E746572616374696F6E5265706F7274696E6755524C73A165636C6963"
      "6B70687474703A2F2F636C69636B2E636F6D71696E74657265737447726F75704E616D65"
      "6E696E7465726573745F67726F757072696E74657265737447726F75704F776E65727819"
      "68747470733A2F2F69672D6F776E65722E636F6D3A31323334";

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(absl::HexStringToBytes(hex_cbor));
  CHECK_OK(decoded_result);

  absl::string_view expected_result =
      R"pb(ad_render_url: "https://ad-found-here.com/ad-1"
           interest_group_name: "interest_group"
           interest_group_owner: "https://ig-owner.com:1234"
           score: 2.35
           bid: 10.21
           bid_currency: "USD"
           win_reporting_urls {
             buyer_reporting_urls {
               reporting_url: "http://reportWin.com"
               interaction_reporting_urls {
                 key: "click"
                 value: "http://click.com"
               }
             }
             top_level_seller_reporting_urls {
               reporting_url: "http://reportResult.com"
               interaction_reporting_urls {
                 key: "click"
                 value: "http://click.com"
               }
             }
             component_seller_reporting_urls {
               reporting_url: "http://reportResult.com"
               interaction_reporting_urls {
                 key: "click"
                 value: "http://click.com"
               }
             }
           }
           update_groups {
             key: "https://ig-owner.com"
             value {
               interest_groups { index: 1 update_if_older_than_ms: 1000 }
             }
           }
           bidding_groups {
             key: "https://ig-owner.com:1234"
             value { index: 2 }
           })pb";
  EXPECT_THAT(*decoded_result, ProtoEq(expected_result));
}

TEST(CborDecodeAuctionResultToProto, ErrorResult) {
  absl::string_view hex_cbor =
      "A1656572726F72A264636F6465190190676D657373616765684261644572726F72";
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(absl::HexStringToBytes(hex_cbor));
  CHECK_OK(decoded_result);

  absl::string_view expected_result =
      R"pb(
    error { code: 400 message: "BadError" })pb";
  EXPECT_THAT(*decoded_result, ProtoEq(expected_result));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
