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

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers_cc_proto_builder.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/compression/gzip.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/common/test/utils/cbor_test_utils.h"
#include "services/common/test/utils/proto_utils.h"
#include "services/common/util/error_accumulator.h"
#include "services/common/util/request_response_constants.h"
#include "services/common/util/scoped_cbor.h"
#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"
#include "services/seller_frontend_service/test/constants.h"
#include "services/seller_frontend_service/test/kanon_test_utils.h"
#include "services/seller_frontend_service/util/buyer_input_proto_utils.h"
#include "services/seller_frontend_service/util/cbor_common_util.h"
#include "src/core/test/utils/proto_test_utils.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

RequestLogContext log_context{{}, server_common::ConsentedDebugConfiguration()};

using BiddingGroupMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using InteractionUrlMap = ::google::protobuf::Map<std::string, std::string>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using ::google::scp::core::test::EqualsProto;
using ::testing::ContainsRegex;
using ::testing::Not;

constexpr char kTestBuyerOrigin[] = "https://buyer-adtech.com";
constexpr char kTestSellerOrigin[] = "https://seller-adtech.com";
constexpr char kTestTopLevelSellerOrigin[] = "https://top-level-adtech.com";
constexpr char kTestRender[] = "https://buyer-adtech.com/ad-1";
constexpr char kTestInterestGroupName[] = "interest_group";
constexpr char kTestAdMetadata[] = "ad_metadata";

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
  if (cbor_interest_group_config.recency) {
    EXPECT_TRUE(cbor_map_add(
        browser_signals,
        BuildIntMapPair(kRecency, cbor_interest_group_config.recency.value())));
  }
  if (cbor_interest_group_config.recency_ms) {
    EXPECT_TRUE(cbor_map_add(
        browser_signals,
        BuildIntMapPair(kRecencyMs,
                        cbor_interest_group_config.recency_ms.value())));
  }

  // Build prevWins arrays.
  cbor_item_t* child_arr_1 = cbor_new_definite_array(2);
  int child_arr_1_int_val = -20;
  EXPECT_TRUE(cbor_array_push(
      child_arr_1, cbor_move(cbor_build_uint64(child_arr_1_int_val))));
  EXPECT_TRUE(cbor_array_push(
      child_arr_1, cbor_move(cbor_build_stringn(
                       kSampleAdRenderId1, sizeof(kSampleAdRenderId1) - 1))));
  cbor_item_t* child_arr_2 = cbor_new_definite_array(2);
  int child_arr_2_int_val = -100;
  EXPECT_TRUE(cbor_array_push(
      child_arr_2, cbor_move(cbor_build_uint64(child_arr_2_int_val))));
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

ScoreAdsResponse::AdScore GetTestAdScore(bool with_reporting_urls = true) {
  // Setup a winning bid.
  ScoreAdsResponse::AdScore winner =
      ScoreAdsResponse_AdScoreBuilder()
          .SetRender(kTestRender)
          .SetDesirability(2.35)
          .SetBidCurrency(kUsdIso)
          .SetBuyerBid(1.21)
          .SetInterestGroupName(kTestInterestGroupName)
          .SetInterestGroupOwner(kTestBuyerOrigin);
  if (with_reporting_urls) {
    winner = ScoreAdsResponse_AdScoreBuilder(winner).SetWinReportingUrls(
        WinReportingUrlsBuilder()
            .SetBuyerReportingUrls(WinReportingUrls_ReportingUrlsBuilder()
                                       .SetReportingUrl(kTestReportWinUrl)
                                       .InsertInteractionReportingUrls(
                                           {kTestEvent1, kTestInteractionUrl1}))
            .SetTopLevelSellerReportingUrls(
                WinReportingUrls_ReportingUrlsBuilder()
                    .SetReportingUrl(kTestReportResultUrl)
                    .InsertInteractionReportingUrls(
                        {kTestEvent1, kTestInteractionUrl1})));
  }
  return winner;
}

ScoreAdsResponse::AdScore GetTestComponentAdScore() {
  // Setup a winning bid.
  ScoreAdsResponse::AdScore winner =
      ScoreAdsResponse_AdScoreBuilder()
          .SetRender(kTestRender)
          .SetDesirability(2.35)
          .SetBid(10.21)
          .SetBidCurrency(kUsdIso)
          .SetBuyerBid(1.21)
          .SetInterestGroupName(kTestInterestGroupName)
          .SetInterestGroupOwner(kTestBuyerOrigin)
          .SetAdMetadata(kTestAdMetadata)
          .SetAllowComponentAuction(true)
          .SetWinReportingUrls(
              WinReportingUrlsBuilder()
                  .SetBuyerReportingUrls(
                      WinReportingUrls_ReportingUrlsBuilder()
                          .SetReportingUrl(kTestReportWinUrl)
                          .InsertInteractionReportingUrls(
                              {kTestEvent1, kTestInteractionUrl1}))
                  .SetTopLevelSellerReportingUrls(
                      WinReportingUrls_ReportingUrlsBuilder()
                          .SetReportingUrl(kTestReportResultUrl)
                          .InsertInteractionReportingUrls(
                              {kTestEvent1, kTestInteractionUrl1}))
                  .SetComponentSellerReportingUrls(
                      WinReportingUrls_ReportingUrlsBuilder()
                          .SetReportingUrl(kTestReportResultUrl)
                          .InsertInteractionReportingUrls(
                              {kTestEvent1, kTestInteractionUrl1})));
  return winner;
}

PrivateAggregateReportingResponses GetTestPrivateAggregateReportingResponses() {
  PrivateAggregateReportingResponse buyer_pagg_response;
  PrivateAggregateContribution contribution1 =
      GetTestContributionWithIntegers(EVENT_TYPE_CUSTOM, kTestEvent1);
  contribution1.set_ig_idx(1);
  *buyer_pagg_response.add_contributions() = contribution1;
  *buyer_pagg_response.add_contributions() = std::move(contribution1);
  buyer_pagg_response.set_adtech_origin(kTestBuyerOrigin);
  PrivateAggregateReportingResponses responses;
  responses.Add()->CopyFrom(buyer_pagg_response);
  return responses;
}

google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
GetTestBiddingGroupMap() {
  AuctionResult::InterestGroupIndex indices;
  indices.add_index(7);
  indices.add_index(2);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  bidding_group_map.try_emplace(kTestBuyerOrigin, indices);
  bidding_group_map.try_emplace("owner2", indices);
  bidding_group_map.try_emplace("owner1", std::move(indices));
  return bidding_group_map;
}

UpdateGroupMap GetTestUpdateGroupMap() {
  UpdateGroupMap update_group_map;
  UpdateInterestGroupList update_list;
  UpdateInterestGroup update_first;
  update_first.set_index(0);
  update_first.set_update_if_older_than_ms(100000);
  UpdateInterestGroup update_second;
  update_second.set_index(1);
  update_second.set_update_if_older_than_ms(500000);
  *update_list.mutable_interest_groups()->Add() = std::move(update_first);
  *update_list.mutable_interest_groups()->Add() = std::move(update_second);
  update_group_map.emplace(kTestBuyerOrigin, std::move(update_list));
  return update_group_map;
}

AdtechOriginDebugUrlsMap GetTestAdtechOriginDebugUrlsMap() {
  AdtechOriginDebugUrlsMap adtech_origin_debug_urls_map;
  adtech_origin_debug_urls_map[kTestSellerOrigin] =
      DebugReportsBuilder()
          .AddReports(DebugReports_DebugReportBuilder()
                          .SetUrl("https://seller-adtech.com/win")
                          .SetIsWinReport(true)
                          .SetIsSellerReport(true)
                          .SetIsComponentWin(true))
          .AddReports(DebugReports_DebugReportBuilder()
                          .SetUrl("https://seller-adtech.com/loss")
                          .SetIsWinReport(false)
                          .SetIsSellerReport(true)
                          .SetIsComponentWin(true));
  adtech_origin_debug_urls_map[kTestBuyerOrigin] =
      DebugReportsBuilder()
          .AddReports(DebugReports_DebugReportBuilder()
                          .SetUrl("https://buyer-adtech.com/win")
                          .SetIsWinReport(true)
                          .SetIsSellerReport(false)
                          .SetIsComponentWin(true))
          .AddReports(DebugReports_DebugReportBuilder()
                          .SetUrl("https://buyer-adtech.com/loss")
                          .SetIsWinReport(false)
                          .SetIsSellerReport(false)
                          .SetIsComponentWin(true));
  return adtech_origin_debug_urls_map;
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
  if (cbor_interest_group_config.recency) {
    signals->set_recency(cbor_interest_group_config.recency.value());
  }
  if (cbor_interest_group_config.recency_ms) {
    signals->set_recency_ms(cbor_interest_group_config.recency_ms.value());
  }

  std::string prev_wins_json_str = absl::StrFormat(
      R"([[-20,"%s"],[-100,"%s"]])", kSampleAdRenderId1, kSampleAdRenderId2);
  signals->set_prev_wins(prev_wins_json_str);

  BuyerInputForBidding buyer_input;
  *buyer_input.add_interest_groups() = ToInterestGroupForBidding(expected_ig);
  google::protobuf::Map<std::string, BuyerInputForBidding> buyer_inputs;
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
    BuyerInputForBidding actual_buyer_input =
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

  absl::flat_hash_map<absl::string_view, BuyerInputForBidding> buyer_inputs =
      DecodeBuyerInputs(actual.buyer_input(), error_accumulator);
  ASSERT_TRUE(error_accumulator.HasErrors());
  EXPECT_TRUE(ContainsClientError(
      error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
      kMalformedCompressedBytestring));
}

TEST(ChromeResponseUtils, VerifyBiddingGroupBuyerOriginOrdering) {
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map = GetTestBiddingGroupMap();

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
  EXPECT_EQ(observed_origins[0], "owner1");
  EXPECT_EQ(observed_origins[1], "owner2");
  EXPECT_EQ(observed_origins[2], kTestBuyerOrigin);
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

TEST(ChromeResponseUtils, CborWithOnlySellerReportingUrls) {
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

TEST(ChromeResponseUtils, VerifyCborEncoding) {
  ScoreAdsResponse::AdScore winner = GetTestAdScore();
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(2);
  bidding_group_map.try_emplace(winner.interest_group_owner(),
                                std::move(ig_indices));
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();

  auto response_with_cbor =
      Encode(winner, bidding_group_map, update_group_map,
             /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  ABSL_LOG(INFO) << "Encoded CBOR: "
                 << absl::BytesToHexString(*response_with_cbor);
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(
        ad_render_url: "https://buyer-adtech.com/ad-1"
        interest_group_name: "interest_group"
        interest_group_owner: "https://buyer-adtech.com"
        score: 2.35
        bid: 1.21
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
        }
        bidding_groups {
          key: "https://buyer-adtech.com"
          value { index: 2 }
        }
        update_groups {
          key: "https://buyer-adtech.com"
          value {
            interest_groups { update_if_older_than_ms: 100000 }
            interest_groups { index: 1 update_if_older_than_ms: 500000 }
          }
        }
      )pb");

  EXPECT_THAT(*decoded_result, EqualsProto(expected));
}

TEST(ChromeResponseUtils, SerializesBothWinnerAndKAnonGhostWinner) {
  ScoreAdsResponse::AdScore winner =
      GetTestAdScore(/*with_reporting_urls=*/false);

  std::unique_ptr<KAnonAuctionResultData> kanon_auction_result_data =
      SampleKAnonAuctionResultData(
          {.ig_index = kSampleIgIndex,
           .ig_owner = kSampleIgOwner,
           .ig_name = kSampleIgName,
           .bucket_name = kSampleBucket,
           .bucket_value = kSampleValue,
           .ad_render_url = kSampleAdRenderUrl,
           .ad_component_render_url = kSampleAdComponentRenderUrl,
           .modified_bid = kSampleModifiedBid,
           .bid_currency = kSampleBidCurrency,
           .ad_metadata = kSampleAdMetadata,
           .buyer_reporting_id = kSampleBuyerReportingId,
           .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId,
           .selected_buyer_and_seller_reporting_id =
               kSampleSelectedBuyerAndSellerReportingId,
           .ad_render_url_hash = std::vector<uint8_t>(
               kSampleAdRenderUrlHash.begin(), kSampleAdRenderUrlHash.end()),
           .ad_component_render_urls_hash =
               std::vector<uint8_t>(kSampleAdComponentRenderUrlsHash.begin(),
                                    kSampleAdComponentRenderUrlsHash.end()),
           .reporting_id_hash = std::vector<uint8_t>(
               kSampleReportingIdHash.begin(), kSampleReportingIdHash.end()),
           .winner_positional_index = kSampleWinnerPositionalIndex});

  auto response_with_cbor = Encode(
      winner, /*bidding_group_map=*/{}, /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100, "",
      std::move(kanon_auction_result_data));
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  ABSL_LOG(INFO) << "Encoded CBOR: "
                 << absl::BytesToHexString(*response_with_cbor);
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  // Both winner and ghost winner and no chaff is set.
  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(
        ad_render_url: "https://buyer-adtech.com/ad-1"
        interest_group_name: "interest_group"
        interest_group_owner: "https://buyer-adtech.com"
        score: 2.35
        bid: 1.21
        k_anon_winner_join_candidates {
          ad_render_url_hash: "\001\002\003\004\005"
          ad_component_render_urls_hash: "\005\004\003\002\001"
          reporting_id_hash: "\002\004\006\010\t"
        }
        k_anon_winner_positional_index: 5
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\001\002\003\004\005"
            ad_component_render_urls_hash: "\005\004\003\002\001"
            reporting_id_hash: "\002\004\006\010\t"
          }
          interest_group_index: 10
          owner: "foo_owner"
          ig_name: "foo_ig_name"
          ghost_winner_private_aggregation_signals {
            bucket: "bucket"
            value: 21
          }
          ghost_winner_for_top_level_auction {
            ad_render_url: "adRenderUrl"
            ad_component_render_urls: "adComponentRenderUrl"
            modified_bid: 1.23
            bid_currency: "bidCurrency"
            ad_metadata: "adMetadata"
            buyer_reporting_id: "buyerReportingId"
            selected_buyer_and_seller_reporting_id: "selectedBuyerAndSellerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
      )pb");

  EXPECT_THAT(*decoded_result, EqualsProto(expected));
}

TEST(ChromeResponseUtils, SerializesKAnonGhostWinnerAndDoesntSetChaff) {
  std::unique_ptr<KAnonAuctionResultData> kanon_auction_result_data =
      SampleKAnonAuctionResultData(
          {.ig_index = kSampleIgIndex,
           .ig_owner = kSampleIgOwner,
           .ig_name = kSampleIgName,
           .bucket_name = kSampleBucket,
           .bucket_value = kSampleValue,
           .ad_render_url = kSampleAdRenderUrl,
           .ad_component_render_url = kSampleAdComponentRenderUrl,
           .modified_bid = kSampleModifiedBid,
           .bid_currency = kSampleBidCurrency,
           .ad_metadata = kSampleAdMetadata,
           .buyer_reporting_id = kSampleBuyerReportingId,
           .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId,
           .selected_buyer_and_seller_reporting_id =
               kSampleSelectedBuyerAndSellerReportingId,
           .ad_render_url_hash = std::vector<uint8_t>(
               kSampleAdRenderUrlHash.begin(), kSampleAdRenderUrlHash.end()),
           .ad_component_render_urls_hash =
               std::vector<uint8_t>(kSampleAdComponentRenderUrlsHash.begin(),
                                    kSampleAdComponentRenderUrlsHash.end()),
           .reporting_id_hash = std::vector<uint8_t>(
               kSampleReportingIdHash.begin(), kSampleReportingIdHash.end()),
           .winner_positional_index = kSampleWinnerPositionalIndex});

  auto response_with_cbor =
      Encode(/*high_score=*/
             std::nullopt, /*bidding_group_map=*/{},
             /*update_group_map=*/{},
             /*error=*/std::nullopt, [](const grpc::Status& status) {},
             /*per_adtech_paapi_contributions_limit=*/100,
             kSampleAdAuctionResultNonce, std::move(kanon_auction_result_data));
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  ABSL_LOG(INFO) << "Encoded CBOR: "
                 << absl::BytesToHexString(*response_with_cbor);
  absl::StatusOr<std::pair<AuctionResult, std::string>> decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  // Ghost winner is present and no chaff is set.
  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\001\002\003\004\005"
            ad_component_render_urls_hash: "\005\004\003\002\001"
            reporting_id_hash: "\002\004\006\010\t"
          }
          interest_group_index: 10
          owner: "foo_owner"
          ig_name: "foo_ig_name"
          ghost_winner_private_aggregation_signals {
            bucket: "bucket"
            value: 21
          }
          ghost_winner_for_top_level_auction {
            ad_render_url: "adRenderUrl"
            ad_component_render_urls: "adComponentRenderUrl"
            modified_bid: 1.23
            bid_currency: "bidCurrency"
            ad_metadata: "adMetadata"
            buyer_reporting_id: "buyerReportingId"
            selected_buyer_and_seller_reporting_id: "selectedBuyerAndSellerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
      )pb");

  EXPECT_THAT(decoded_result->first, EqualsProto(expected));
  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils, CborEncodesComponentAuctionResult) {
  ScoreAdsResponse_AdScore winner = GetTestComponentAdScore();
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map;
  AuctionResult::InterestGroupIndex ig_indices;
  ig_indices.add_index(2);
  bidding_group_map.try_emplace(winner.interest_group_owner(),
                                std::move(ig_indices));
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();
  AdtechOriginDebugUrlsMap adtech_origin_debug_urls_map =
      GetTestAdtechOriginDebugUrlsMap();

  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, winner, bidding_group_map, update_group_map,
      adtech_origin_debug_urls_map,
      /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(
        ad_render_url: "https://buyer-adtech.com/ad-1"
        interest_group_name: "interest_group"
        interest_group_owner: "https://buyer-adtech.com"
        score: 2.35
        bid: 10.21
        win_reporting_urls {
          buyer_reporting_urls {
            reporting_url: "http://reportWin.com"
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
          top_level_seller_reporting_urls {
            reporting_url: "http://reportResult.com"
            interaction_reporting_urls {
              key: "click"
              value: "http://click.com"
            }
          }
        }
        adtech_origin_debug_urls_map {
          key: "https://buyer-adtech.com"
          value {
            reports {
              url: "https://buyer-adtech.com/win"
              is_win_report: true
              is_component_win: true
            }
            reports {
              url: "https://buyer-adtech.com/loss"
              is_component_win: true
            }
          }
        }
        adtech_origin_debug_urls_map {
          key: "https://seller-adtech.com"
          value {
            reports {
              url: "https://seller-adtech.com/win"
              is_win_report: true
              is_seller_report: true
              is_component_win: true
            }
            reports {
              url: "https://seller-adtech.com/loss"
              is_seller_report: true
              is_component_win: true
            }
          }
        }
        bidding_groups {
          key: "https://buyer-adtech.com"
          value { index: 2 }
        }
        top_level_seller: "https://top-level-adtech.com"
        ad_metadata: "ad_metadata"
        bid_currency: "USD"
        update_groups {
          key: "https://buyer-adtech.com"
          value {
            interest_groups { update_if_older_than_ms: 100000 }
            interest_groups { index: 1 update_if_older_than_ms: 500000 }
          }
        }
      )pb");

  EXPECT_THAT(*decoded_result, EqualsProto(expected));
}

// Empty string causes the auction result to be rejected by Chrome.
TEST(ChromeResponseUtils, DoesNotEncodeEmptyAdMetadataForComponentAuction) {
  ScoreAdsResponse::AdScore winner = GetTestComponentAdScore();
  winner.clear_ad_metadata();

  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, winner, /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {});
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  // Ad metadata key should not be present in cbor
  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdMetadata)));
}

TEST(ChromeResponseUtils, VerifyCborEncodedError) {
  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);

  auto response_with_cbor =
      Encode(/*high_score=*/std::nullopt, /*bidding_group_map=*/{},
             /*update_group_map=*/{}, error, [](const grpc::Status& status) {});

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();
  EXPECT_EQ(decoded_result->error().message(), kSampleErrorMessage);
  EXPECT_EQ(decoded_result->error().code(), kSampleErrorCode);
}

TEST(ChromeResponseUtils, EncodesNonceWithError) {
  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);

  auto response_with_cbor = Encode(
      /*high_score=*/
      std::nullopt, /*bidding_group_map=*/{},
      /*update_group_map=*/{}, error, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      kSampleAdAuctionResultNonce);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils, EncodesNonceWithChaff) {
  auto response_with_cbor = Encode(
      /*high_score=*/
      std::nullopt, /*bidding_group_map=*/{},
      /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      kSampleAdAuctionResultNonce);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils, EncodesNonceWithAdScore) {
  ScoreAdsResponse::AdScore winner = GetTestAdScore();

  auto response_with_cbor = Encode(
      winner, /*bidding_group_map=*/{}, /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      kSampleAdAuctionResultNonce);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils, DoesNotContainNonceWithErrorWhenEmpty) {
  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);

  auto response_with_cbor = Encode(
      /*high_score=*/
      std::nullopt, /*bidding_group_map=*/{},
      /*update_group_map=*/{}, error, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      /*ad_auction_result_nonce=*/"");
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdAuctionResultNonce)));

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_TRUE(decoded_result->second.empty());
}

TEST(ChromeResponseUtils, DoesNotContainNonceWithChaffWhenEmpty) {
  auto response_with_cbor = Encode(
      /*high_score=*/
      std::nullopt, /*bidding_group_map=*/{},
      /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      /*ad_auction_result_nonce=*/"");
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdAuctionResultNonce)));

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_TRUE(decoded_result->second.empty());
}

TEST(ChromeResponseUtils, DoesNotContainNonceWithAdScoreWhenEmpty) {
  ScoreAdsResponse::AdScore winner = GetTestAdScore();

  auto response_with_cbor = Encode(
      winner, /*bidding_group_map=*/{}, /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      /*ad_auction_result_nonce=*/"");
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdAuctionResultNonce)));

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_TRUE(decoded_result->second.empty());
}

TEST(ChromeResponseUtils, EncodesNonceWithComponentAuctionError) {
  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);
  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, /*high_score=*/std::nullopt,
      /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{}, error,
      [](const grpc::Status& status) {}, kSampleAdAuctionResultNonce);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils, EncodesNonceWithComponentAuctionChaff) {
  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, /*high_score=*/std::nullopt,
      /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      kSampleAdAuctionResultNonce);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils, EncodesNonceWithComponentAuctionAdScore) {
  ScoreAdsResponse::AdScore winner = GetTestComponentAdScore();

  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, winner, /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      kSampleAdAuctionResultNonce);
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_EQ(decoded_result->second, kSampleAdAuctionResultNonce);
}

TEST(ChromeResponseUtils,
     DoesNotContainNonceWithComponentAuctionErrorWhenEmpty) {
  AuctionResult::Error error;
  error.set_message(kSampleErrorMessage);
  error.set_code(kSampleErrorCode);
  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, /*high_score=*/std::nullopt,
      /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{}, error,
      [](const grpc::Status& status) {},
      /*ad_auction_result_nonce=*/"");
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdAuctionResultNonce)));

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_TRUE(decoded_result->second.empty());
}

TEST(ChromeResponseUtils,
     DoesNotContainNonceWithComponentAuctionChaffWhenEmpty) {
  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, /*high_score=*/std::nullopt,
      /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*ad_auction_result_nonce=*/"");
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdAuctionResultNonce)));

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_TRUE(decoded_result->second.empty());
}

TEST(ChromeResponseUtils,
     DoesNotContainNonceWithComponentAuctionAdScoreWhenEmpty) {
  ScoreAdsResponse::AdScore winner = GetTestComponentAdScore();

  auto response_with_cbor = EncodeComponent(
      kTestTopLevelSellerOrigin, winner, /*bidding_group_map=*/{},
      /*update_group_map=*/{}, /*adtech_origin_debug_urls_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*ad_auction_result_nonce=*/"");
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();

  EXPECT_THAT(*response_with_cbor, Not(ContainsRegex(kAdAuctionResultNonce)));

  auto decoded_result =
      CborDecodeAuctionResultAndNonceToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  EXPECT_TRUE(decoded_result->second.empty());
}

TEST(ChromeResponseUtils, EncodesKAnonData) {
  std::unique_ptr<KAnonAuctionResultData> kanon_auction_result_data =
      SampleKAnonAuctionResultData(
          {.ig_index = kSampleIgIndex,
           .ig_owner = kSampleIgOwner,
           .ig_name = kSampleIgName,
           .bucket_name = kSampleBucket,
           .bucket_value = kSampleValue,
           .ad_render_url = kSampleAdRenderUrl,
           .ad_component_render_url = kSampleAdComponentRenderUrl,
           .modified_bid = kSampleModifiedBid,
           .bid_currency = kSampleBidCurrency,
           .ad_metadata = kSampleAdMetadata,
           .buyer_reporting_id = kSampleBuyerReportingId,
           .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId,
           .selected_buyer_and_seller_reporting_id =
               kSampleSelectedBuyerAndSellerReportingId,
           .ad_render_url_hash = std::vector<uint8_t>(
               kSampleAdRenderUrlHash.begin(), kSampleAdRenderUrlHash.end()),
           .ad_component_render_urls_hash =
               std::vector<uint8_t>(kSampleAdComponentRenderUrlsHash.begin(),
                                    kSampleAdComponentRenderUrlsHash.end()),
           .reporting_id_hash = std::vector<uint8_t>(
               kSampleReportingIdHash.begin(), kSampleReportingIdHash.end()),
           .winner_positional_index = kSampleWinnerPositionalIndex});

  ScoreAdsResponse::AdScore winner;
  auto response_with_cbor = Encode(
      winner, /*bidding_group_map=*/{}, /*update_group_map=*/{},
      /*error=*/std::nullopt, [](const grpc::Status& status) {},
      /*per_adtech_paapi_contributions_limit=*/100,
      /*ad_auction_result_nonce=*/"", std::move(kanon_auction_result_data));
  ASSERT_TRUE(response_with_cbor.ok()) << response_with_cbor.status();
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(*response_with_cbor);
  ASSERT_TRUE(decoded_result.ok()) << decoded_result.status();

  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(
        k_anon_winner_join_candidates {
          ad_render_url_hash: "\001\002\003\004\005"
          ad_component_render_urls_hash: "\005\004\003\002\001"
          reporting_id_hash: "\002\004\006\010\t"
        }
        k_anon_winner_positional_index: 5
        k_anon_ghost_winners {
          k_anon_join_candidates {
            ad_render_url_hash: "\001\002\003\004\005"
            ad_component_render_urls_hash: "\005\004\003\002\001"
            reporting_id_hash: "\002\004\006\010\t"
          }
          interest_group_index: 10
          owner: "foo_owner"
          ig_name: "foo_ig_name"
          ghost_winner_private_aggregation_signals {
            bucket: "bucket"
            value: 21
          }
          ghost_winner_for_top_level_auction {
            ad_render_url: "adRenderUrl"
            ad_component_render_urls: "adComponentRenderUrl"
            modified_bid: 1.23
            bid_currency: "bidCurrency"
            ad_metadata: "adMetadata"
            buyer_reporting_id: "buyerReportingId"
            selected_buyer_and_seller_reporting_id: "selectedBuyerAndSellerReportingId"
            buyer_and_seller_reporting_id: "buyerAndSellerReportingId"
          }
        }
      )pb");

  EXPECT_THAT(*decoded_result, EqualsProto(expected));
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
  ScoreAdsResponse::AdScore winner = GetTestAdScore();
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map = GetTestBiddingGroupMap();
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();
  std::unique_ptr<KAnonAuctionResultData> kanon_auction_result_data =
      SampleKAnonAuctionResultData(
          {.ig_index = kSampleIgIndex,
           .ig_owner = kSampleIgOwner,
           .ig_name = kSampleIgName,
           .bucket_name = kSampleBucket,
           .bucket_value = kSampleValue,
           .ad_render_url = kSampleAdRenderUrl,
           .ad_component_render_url = kSampleAdComponentRenderUrl,
           .modified_bid = kSampleModifiedBid,
           .bid_currency = kSampleBidCurrency,
           .ad_metadata = kSampleAdMetadata,
           .buyer_reporting_id = kSampleBuyerReportingId,
           .buyer_and_seller_reporting_id = kSampleBuyerAndSellerReportingId,
           .selected_buyer_and_seller_reporting_id =
               kSampleSelectedBuyerAndSellerReportingId,
           .ad_render_url_hash = std::vector<uint8_t>(
               kSampleAdRenderUrlHash.begin(), kSampleAdRenderUrlHash.end()),
           .ad_component_render_urls_hash =
               std::vector<uint8_t>(kSampleAdComponentRenderUrlsHash.begin(),
                                    kSampleAdComponentRenderUrlsHash.end()),
           .reporting_id_hash = std::vector<uint8_t>(
               kSampleReportingIdHash.begin(), kSampleReportingIdHash.end()),
           .winner_positional_index = kSampleWinnerPositionalIndex});

  auto ret = Encode(
      std::move(winner), bidding_group_map, update_group_map,
      /*error=*/std::nullopt, [](auto error) {},
      /*per_adtech_paapi_contributions_limit=*/100, kSampleAdAuctionResultNonce,
      std::move(kanon_auction_result_data));

  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ae63626964fa3f9ae148656e6f6e63657761642d61756374696f6e2d726573756c74"
          "2d6e306e63656573636f7265fa401666666769734368616666f46a636f6d706f6e65"
          "6e7473806b616452656e64657255524c781d68747470733a2f2f62757965722d6164"
          "746563682e636f6d2f61642d316c75706461746547726f757073a178186874747073"
          "3a2f2f62757965722d6164746563682e636f6d82a265696e64657800737570646174"
          "6549664f6c6465725468616e4d731a000186a0a265696e6465780173757064617465"
          "49664f6c6465725468616e4d731a0007a1206d62696464696e6747726f757073a366"
          "6f776e657231820702666f776e657232820702781868747470733a2f2f6275796572"
          "2d6164746563682e636f6d8207027077696e5265706f7274696e6755524c73a27262"
          "757965725265706f7274696e6755524c73a26c7265706f7274696e6755524c746874"
          "74703a2f2f7265706f727457696e2e636f6d7818696e746572616374696f6e526570"
          "6f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e636f6d"
          "781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a26c726570"
          "6f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e636f6d78"
          "18696e746572616374696f6e5265706f7274696e6755524c73a165636c69636b7068"
          "7474703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e616d656e"
          "696e7465726573745f67726f7570716b416e6f6e47686f737457696e6e65727381a6"
          "656f776e657269666f6f5f6f776e657271696e74657265737447726f75704e616d65"
          "6b666f6f5f69675f6e616d6572696e74657265737447726f7570496e6465780a736b"
          "416e6f6e4a6f696e43616e64696461746573a36f616452656e64657255524c486173"
          "684501020304056f7265706f7274696e674964486173684502040608097819616443"
          "6f6d706f6e656e7452656e64657255524c734861736881450504030201781d67686f"
          "737457696e6e6572466f72546f704c6576656c41756374696f6ea86a61644d657461"
          "646174616a61644d657461646174616b616452656e64657255524c6b616452656e64"
          "657255726c6b62696443757272656e63796b62696443757272656e63796b6d6f6469"
          "66696564426964fa3f9d70a47062757965725265706f7274696e6749647062757965"
          "725265706f7274696e674964756164436f6d706f6e656e7452656e64657255524c73"
          "81746164436f6d706f6e656e7452656e64657255726c78196275796572416e645365"
          "6c6c65725265706f7274696e67496478196275796572416e6453656c6c6572526570"
          "6f7274696e674964782173656c65637465644275796572416e6453656c6c65725265"
          "706f7274696e674964782173656c65637465644275796572416e6453656c6c657252"
          "65706f7274696e674964782467686f737457696e6e65725072697661746541676772"
          "65676174696f6e5369676e616c73a26576616c756515666275636b6574666275636b"
          "657472696e74657265737447726f75704f776e6572781868747470733a2f2f627579"
          "65722d6164746563682e636f6d78196b416e6f6e57696e6e65724a6f696e43616e64"
          "696461746573a36f616452656e64657255524c486173684501020304056f7265706f"
          "7274696e6749644861736845020406080978196164436f6d706f6e656e7452656e64"
          "657255524c734861736881450504030201781a6b416e6f6e57696e6e6572506f7369"
          "74696f6e616c496e64657805"));
}

TEST(ChromeResponseUtils, VerifyMinimalResponseEncodingWithBuyerReportingId) {
  ScoreAdsResponse::AdScore winner = GetTestAdScore();
  winner.set_buyer_reporting_id(kTestBuyerReportingId);
  PrivateAggregateReportingResponses responses =
      GetTestPrivateAggregateReportingResponses();
  auto* top_level_contributions = winner.mutable_top_level_contributions();
  *top_level_contributions = std::move(responses);
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map = GetTestBiddingGroupMap();
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();

  auto ret = Encode(
      std::move(winner), bidding_group_map, update_group_map,
      /*error=*/std::nullopt, [](auto error) {},
      /*per_adtech_paapi_contributions_limit=*/1);
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ac63626964fa3f9ae1486573636f7265fa401666666769734368616666f46a636f6d"
          "706f6e656e7473806b616452656e64657255524c781d68747470733a2f2f62757965"
          "722d6164746563682e636f6d2f61642d316c70616767526573706f6e736581a26f69"
          "67436f6e747269627574696f6e7381a2676967496e64657801726576656e74436f6e"
          "747269627574696f6e7381a2656576656e7465636c69636b6d636f6e747269627574"
          "696f6e7381a26576616c75650a666275636b6574500123456789abcdef123456789a"
          "bcdef06f7265706f7274696e674f726967696e781868747470733a2f2f6275796572"
          "2d6164746563682e636f6d6c75706461746547726f757073a1781868747470733a2f"
          "2f62757965722d6164746563682e636f6d82a265696e646578007375706461746549"
          "664f6c6465725468616e4d731a000186a0a265696e64657801737570646174654966"
          "4f6c6465725468616e4d731a0007a1206d62696464696e6747726f757073a3666f77"
          "6e657231820702666f776e657232820702781868747470733a2f2f62757965722d61"
          "64746563682e636f6d8207027062757965725265706f7274696e6749647474657374"
          "42757965725265706f7274696e6749647077696e5265706f7274696e6755524c73a2"
          "7262757965725265706f7274696e6755524c73a26c7265706f7274696e6755524c74"
          "687474703a2f2f7265706f727457696e2e636f6d7818696e746572616374696f6e52"
          "65706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e63"
          "6f6d781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a26c72"
          "65706f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e636f"
          "6d7818696e746572616374696f6e5265706f7274696e6755524c73a165636c69636b"
          "70687474703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e616d"
          "656e696e7465726573745f67726f757072696e74657265737447726f75704f776e65"
          "72781868747470733a2f2f62757965722d6164746563682e636f6d"));
}

TEST(ChromeResponseUtils, VerifyMinimalComponentResponseEncoding) {
  ScoreAdsResponse::AdScore winner = GetTestComponentAdScore();
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map = GetTestBiddingGroupMap();
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();

  auto ret = EncodeComponent(
      kTestTopLevelSellerOrigin, std::move(winner), bidding_group_map,
      update_group_map, /*adtech_origin_debug_urls_map=*/{},
      /*error=*/std::nullopt, [](auto error) {}, kSampleAdAuctionResultNonce);
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ae63626964fa41235c29656e6f6e63657761642d61756374696f6e2d726573756c74"
          "2d6e306e63656573636f7265fa401666666769734368616666f46a61644d65746164"
          "6174616b61645f6d657461646174616a636f6d706f6e656e7473806b616452656e64"
          "657255524c781d68747470733a2f2f62757965722d6164746563682e636f6d2f6164"
          "2d316b62696443757272656e6379635553446c75706461746547726f757073a17818"
          "68747470733a2f2f62757965722d6164746563682e636f6d82a265696e6465780073"
          "75706461746549664f6c6465725468616e4d731a000186a0a265696e646578017375"
          "706461746549664f6c6465725468616e4d731a0007a1206d62696464696e6747726f"
          "757073a3666f776e657231820702666f776e657232820702781868747470733a2f2f"
          "62757965722d6164746563682e636f6d8207026e746f704c6576656c53656c6c6572"
          "781c68747470733a2f2f746f702d6c6576656c2d6164746563682e636f6d7077696e"
          "5265706f7274696e6755524c73a37262757965725265706f7274696e6755524c73a2"
          "6c7265706f7274696e6755524c74687474703a2f2f7265706f727457696e2e636f6d"
          "7818696e746572616374696f6e5265706f7274696e6755524c73a165636c69636b70"
          "687474703a2f2f636c69636b2e636f6d781b746f704c6576656c53656c6c65725265"
          "706f7274696e6755524c73a26c7265706f7274696e6755524c77687474703a2f2f72"
          "65706f7274526573756c742e636f6d7818696e746572616374696f6e5265706f7274"
          "696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e636f6d781c63"
          "6f6d706f6e656e7453656c6c65725265706f7274696e6755524c73a26c7265706f72"
          "74696e6755524c77687474703a2f2f7265706f7274526573756c742e636f6d781869"
          "6e746572616374696f6e5265706f7274696e6755524c73a165636c69636b70687474"
          "703a2f2f636c69636b2e636f6d71696e74657265737447726f75704e616d656e696e"
          "7465726573745f67726f757072696e74657265737447726f75704f776e6572781868"
          "747470733a2f2f62757965722d6164746563682e636f6d"));
}

TEST(ChromeResponseUtils, VerifyMinimalComponentResponseEncodingNoBidCurrency) {
  ScoreAdsResponse::AdScore winner = GetTestComponentAdScore();
  winner.clear_bid_currency();
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map = GetTestBiddingGroupMap();
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();

  auto ret = EncodeComponent(kTestTopLevelSellerOrigin, std::move(winner),
                             bidding_group_map, update_group_map,
                             /*adtech_origin_debug_urls_map=*/{},
                             /*error=*/std::nullopt, [](auto error) {});
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ac63626964fa41235c296573636f7265fa401666666769734368616666f46a61644d"
          "657461646174616b61645f6d657461646174616a636f6d706f6e656e7473806b6164"
          "52656e64657255524c781d68747470733a2f2f62757965722d6164746563682e636f"
          "6d2f61642d316c75706461746547726f757073a1781868747470733a2f2f62757965"
          "722d6164746563682e636f6d82a265696e646578007375706461746549664f6c6465"
          "725468616e4d731a000186a0a265696e646578017375706461746549664f6c646572"
          "5468616e4d731a0007a1206d62696464696e6747726f757073a3666f776e65723182"
          "0702666f776e657232820702781868747470733a2f2f62757965722d616474656368"
          "2e636f6d8207026e746f704c6576656c53656c6c6572781c68747470733a2f2f746f"
          "702d6c6576656c2d6164746563682e636f6d7077696e5265706f7274696e6755524c"
          "73a37262757965725265706f7274696e6755524c73a26c7265706f7274696e675552"
          "4c74687474703a2f2f7265706f727457696e2e636f6d7818696e746572616374696f"
          "6e5265706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b"
          "2e636f6d781b746f704c6576656c53656c6c65725265706f7274696e6755524c73a2"
          "6c7265706f7274696e6755524c77687474703a2f2f7265706f7274526573756c742e"
          "636f6d7818696e746572616374696f6e5265706f7274696e6755524c73a165636c69"
          "636b70687474703a2f2f636c69636b2e636f6d781c636f6d706f6e656e7453656c6c"
          "65725265706f7274696e6755524c73a26c7265706f7274696e6755524c7768747470"
          "3a2f2f7265706f7274526573756c742e636f6d7818696e746572616374696f6e5265"
          "706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b2e636f"
          "6d71696e74657265737447726f75704e616d656e696e7465726573745f67726f7570"
          "72696e74657265737447726f75704f776e6572781868747470733a2f2f6275796572"
          "2d6164746563682e636f6d"));
}

TEST(ChromeResponseUtils,
     VerifyMinimalComponentResponseEncodingWithDebugReports) {
  ScoreAdsResponse::AdScore winner = GetTestComponentAdScore();
  google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>
      bidding_group_map = GetTestBiddingGroupMap();
  UpdateGroupMap update_group_map = GetTestUpdateGroupMap();
  AdtechOriginDebugUrlsMap adtech_origin_debug_urls_map =
      GetTestAdtechOriginDebugUrlsMap();

  auto ret = EncodeComponent(kTestTopLevelSellerOrigin, std::move(winner),
                             bidding_group_map, update_group_map,
                             adtech_origin_debug_urls_map,
                             /*error=*/std::nullopt, [](auto error) {});
  ASSERT_TRUE(ret.ok()) << ret.status();
  // Conversion can be verified at: https://cbor.me/
  EXPECT_THAT(
      absl::BytesToHexString(*ret),
      testing::StrEq(
          "ae63626964fa41235c296573636f7265fa401666666769734368616666f46a61644d"
          "657461646174616b61645f6d657461646174616a636f6d706f6e656e7473806b6164"
          "52656e64657255524c781d68747470733a2f2f62757965722d6164746563682e636f"
          "6d2f61642d316b62696443757272656e6379635553446c64656275675265706f7274"
          "7382a2677265706f72747382a46375726c781c68747470733a2f2f62757965722d61"
          "64746563682e636f6d2f77696e6b697357696e5265706f7274f56c636f6d706f6e65"
          "6e7457696ef56e697353656c6c65725265706f7274f4a46375726c781d6874747073"
          "3a2f2f62757965722d6164746563682e636f6d2f6c6f73736b697357696e5265706f"
          "7274f46c636f6d706f6e656e7457696ef56e697353656c6c65725265706f7274f46c"
          "6164546563684f726967696e781868747470733a2f2f62757965722d616474656368"
          "2e636f6da2677265706f72747382a46375726c781d68747470733a2f2f73656c6c65"
          "722d6164746563682e636f6d2f77696e6b697357696e5265706f7274f56c636f6d70"
          "6f6e656e7457696ef56e697353656c6c65725265706f7274f5a46375726c781e6874"
          "7470733a2f2f73656c6c65722d6164746563682e636f6d2f6c6f73736b697357696e"
          "5265706f7274f46c636f6d706f6e656e7457696ef56e697353656c6c65725265706f"
          "7274f56c6164546563684f726967696e781968747470733a2f2f73656c6c65722d61"
          "64746563682e636f6d6c75706461746547726f757073a1781868747470733a2f2f62"
          "757965722d6164746563682e636f6d82a265696e646578007375706461746549664f"
          "6c6465725468616e4d731a000186a0a265696e646578017375706461746549664f6c"
          "6465725468616e4d731a0007a1206d62696464696e6747726f757073a3666f776e65"
          "7231820702666f776e657232820702781868747470733a2f2f62757965722d616474"
          "6563682e636f6d8207026e746f704c6576656c53656c6c6572781c68747470733a2f"
          "2f746f702d6c6576656c2d6164746563682e636f6d7077696e5265706f7274696e67"
          "55524c73a37262757965725265706f7274696e6755524c73a26c7265706f7274696e"
          "6755524c74687474703a2f2f7265706f727457696e2e636f6d7818696e7465726163"
          "74696f6e5265706f7274696e6755524c73a165636c69636b70687474703a2f2f636c"
          "69636b2e636f6d781b746f704c6576656c53656c6c65725265706f7274696e675552"
          "4c73a26c7265706f7274696e6755524c77687474703a2f2f7265706f727452657375"
          "6c742e636f6d7818696e746572616374696f6e5265706f7274696e6755524c73a165"
          "636c69636b70687474703a2f2f636c69636b2e636f6d781c636f6d706f6e656e7453"
          "656c6c65725265706f7274696e6755524c73a26c7265706f7274696e6755524c7768"
          "7474703a2f2f7265706f7274526573756c742e636f6d7818696e746572616374696f"
          "6e5265706f7274696e6755524c73a165636c69636b70687474703a2f2f636c69636b"
          "2e636f6d71696e74657265737447726f75704e616d656e696e7465726573745f6772"
          "6f757072696e74657265737447726f75704f776e6572781868747470733a2f2f6275"
          "7965722d6164746563682e636f6d"));
}

TEST(CborDecodeAuctionResultToProto, ValidAuctionResult) {
  absl::string_view hex_cbor =
      "AB63626964FA41235C296573636F7265FA401666666769734368616666F46B616452656E"
      "64657255524C781D68747470733A2F2F62757965722D6164746563682E636F6D2F61642D"
      "316B62696443757272656E6379635553446C70616767526573706F6E736581A16F696743"
      "6F6E747269627574696F6E7381A2676967496E64657801726576656E74436F6E74726962"
      "7574696F6E7381A2656576656E7465636C69636B6D636F6E747269627574696F6E7381A2"
      "6576616C75650A666275636B6574500123456789ABCDEF123456789ABCDEF06C75706461"
      "746547726F757073A1781868747470733A2F2F62757965722D6164746563682E636F6D81"
      "A265696E646578017375706461746549664F6C6465725468616E4D731903E86D62696464"
      "696E6747726F757073A1781868747470733A2F2F62757965722D6164746563682E636F6D"
      "81027077696E5265706F7274696E6755524C73A37262757965725265706F7274696E6755"
      "524C73A26C7265706F7274696E6755524C74687474703A2F2F7265706F727457696E2E63"
      "6F6D7818696E746572616374696F6E5265706F7274696E6755524C73A165636C69636B70"
      "687474703A2F2F636C69636B2E636F6D781B746F704C6576656C53656C6C65725265706F"
      "7274696E6755524C73A26C7265706F7274696E6755524C77687474703A2F2F7265706F72"
      "74526573756C742E636F6D7818696E746572616374696F6E5265706F7274696E6755524C"
      "73A165636C69636B70687474703A2F2F636C69636B2E636F6D781C636F6D706F6E656E74"
      "53656C6C65725265706F7274696E6755524C73A26C7265706F7274696E6755524C776874"
      "74703A2F2F7265706F7274526573756C742E636F6D7818696E746572616374696F6E5265"
      "706F7274696E6755524C73A165636C69636B70687474703A2F2F636C69636B2E636F6D71"
      "696E74657265737447726F75704E616D656E696E7465726573745F67726F757072696E74"
      "657265737447726F75704F776E6572781868747470733A2F2F62757965722D6164746563"
      "682E636F6D";

  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(absl::HexStringToBytes(hex_cbor));
  CHECK_OK(decoded_result);

  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(ad_render_url: "https://buyer-adtech.com/ad-1"
           interest_group_name: "interest_group"
           interest_group_owner: "https://buyer-adtech.com"
           score: 2.35
           bid: 10.21
           bid_currency: "USD"
           top_level_contributions {
             contributions {
               bucket {
                 bucket_128_bit {
                   bucket_128_bits: 1311768467463790320
                   bucket_128_bits: 81985529216486895
                 }
               }
               value { int_value: 10 }
               event { event_type: EVENT_TYPE_CUSTOM event_name: "click" }
               ig_idx: 1
             }
           }
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
             key: "https://buyer-adtech.com"
             value {
               interest_groups { index: 1 update_if_older_than_ms: 1000 }
             }
           }
           bidding_groups {
             key: "https://buyer-adtech.com"
             value { index: 2 }
           })pb");
  EXPECT_THAT(*decoded_result, EqualsProto(expected));
}

TEST(CborDecodeAuctionResultToProto, ErrorResult) {
  absl::string_view hex_cbor =
      "A1656572726F72A264636F6465190190676D657373616765684261644572726F72";
  absl::StatusOr<AuctionResult> decoded_result =
      CborDecodeAuctionResultToProto(absl::HexStringToBytes(hex_cbor));
  CHECK_OK(decoded_result);

  AuctionResult expected = ParseTextOrDie<AuctionResult>(
      R"pb(error { code: 400 message: "BadError" })pb");
  EXPECT_THAT(*decoded_result, EqualsProto(expected));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
