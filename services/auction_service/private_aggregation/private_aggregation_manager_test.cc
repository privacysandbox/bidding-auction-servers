//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/auction_service/private_aggregation/private_aggregation_manager.h"

#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/auction_service/private_aggregation/private_aggregation_manager_test_util.h"
#include "services/common/private_aggregation/private_aggregation_test_util.h"
#include "services/common/util/json_util.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(PrivateAggregationManagerTest, ToBaseValueUnspecified) {
  EXPECT_EQ(ToBaseValue(""), BaseValue::BASE_VALUE_UNSPECIFIED);
  EXPECT_EQ(ToBaseValue("unknown-value"), BaseValue::BASE_VALUE_UNSPECIFIED);
}

TEST(PrivateAggregationManagerTest, ToBaseValueWinningBid) {
  EXPECT_EQ(ToBaseValue("BASE_VALUE_WINNING_BID"),
            BaseValue::BASE_VALUE_WINNING_BID);
}

TEST(PrivateAggregationManagerTest, ToBaseValueHighestScoringOtherBid) {
  EXPECT_EQ(ToBaseValue("BASE_VALUE_HIGHEST_SCORING_OTHER_BID"),
            BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID);
}

TEST(PrivateAggregationManagerTest, ToBaseValueScriptRunTime) {
  EXPECT_EQ(ToBaseValue("BASE_VALUE_SCRIPT_RUN_TIME"),
            BaseValue::BASE_VALUE_SCRIPT_RUN_TIME);
}

TEST(PrivateAggregationManagerTest, ToBaseValueSignalsFetchTime) {
  EXPECT_EQ(ToBaseValue("BASE_VALUE_SIGNALS_FETCH_TIME"),
            BaseValue::BASE_VALUE_SIGNALS_FETCH_TIME);
}

TEST(PrivateAggregationManagerTest, ToBaseValueBidRejectionReason) {
  EXPECT_EQ(ToBaseValue("BASE_VALUE_BID_REJECTION_REASON"),
            BaseValue::BASE_VALUE_BID_REJECTION_REASON);
  EXPECT_EQ(ToBaseValue("unknown-value"), BaseValue::BASE_VALUE_UNSPECIFIED);
  EXPECT_EQ(BaseValue::BASE_VALUE_UNSPECIFIED, 0);
}

TEST(PrivateAggregationManagerTest, EventTypeWinToString) {
  EXPECT_EQ(EventTypeToString(EventType::EVENT_TYPE_WIN), kEventTypeWin);
}

TEST(PrivateAggregationManagerTest, EventTypeLossToString) {
  EXPECT_EQ(EventTypeToString(EventType::EVENT_TYPE_LOSS), kEventTypeLoss);
}

TEST(PrivateAggregationManagerTest, EventTypeAlwaysToString) {
  EXPECT_EQ(EventTypeToString(EventType::EVENT_TYPE_ALWAYS), kEventTypeAlways);
}

TEST(PrivateAggregationManagerTest, EventTypeCustomToString) {
  EXPECT_EQ(EventTypeToString(EventType::EVENT_TYPE_CUSTOM), kEventTypeCustom);
}

TEST(PrivateAggregationManagerTest, EventTypeUnspecifiedToString) {
  EXPECT_EQ(EventTypeToString(EventType::EVENT_TYPE_UNSPECIFIED),
            kEventTypeUnspecified);
}

TEST(HandlePrivateAggregationReportingTest,
     EmptyContributionResponseJSONSuccess) {
  BaseValues base_values = MakeDefaultBaseValues();
  PrivateAggregationHandlerMetadata metadata = {
      .most_desirable_ad_score_id = kTestWinningId.data(),
      .base_values = base_values,
      .ad_id = kTestWinningId.data(),
  };

  ScoreAdsResponse::AdScore score_ads_response;
  rapidjson::Document doc;
  doc.SetObject();
  HandlePrivateAggregationReporting(metadata, doc, score_ads_response);
  EXPECT_EQ(score_ads_response.top_level_contributions(0).contributions_size(),
            0);
}

TEST(HandlePrivateAggregationReportingTest, ValidWinningAdSuccess) {
  BaseValues base_values = MakeDefaultBaseValues();
  PrivateAggregationHandlerMetadata metadata = {
      .most_desirable_ad_score_id = kTestWinningId.data(),
      .base_values = base_values,
      .ad_id = kTestWinningId.data(),
  };

  PrivateAggregateContribution contribution1;
  contribution1.mutable_value()->set_int_value(23);
  contribution1.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);
  contribution1.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);

  PrivateAggregateContribution contribution2;
  contribution2.mutable_value()->mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_WINNING_BID);
  contribution2.mutable_value()->mutable_extended_value()->set_scale(2.0);
  contribution2.mutable_value()->mutable_extended_value()->set_offset(10);
  contribution2.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);
  contribution2.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);

  std::vector<PrivateAggregateContribution> win_contributions = {contribution1,
                                                                 contribution2};
  std::vector<PrivateAggregateContribution> always_contributions = {
      contribution1, contribution2};
  absl::flat_hash_map<std::string, std::vector<PrivateAggregateContribution>>
      custom_event_contributions;
  custom_event_contributions.try_emplace("custom_event_1", win_contributions);

  rapidjson::Document doc = CreateContributionResponseDocument(
      win_contributions, {}, always_contributions, custom_event_contributions);

  ScoreAdsResponse::AdScore score_ads_response;
  HandlePrivateAggregationReporting(metadata, doc, score_ads_response);
  EXPECT_EQ(score_ads_response.top_level_contributions(0).contributions_size(),
            6);
  EXPECT_EQ(score_ads_response.top_level_contributions(0)
                .contributions(0)
                .value()
                .int_value(),
            contribution1.value().int_value());
  EXPECT_EQ(
      score_ads_response.top_level_contributions(0)
          .contributions(1)
          .value()
          .int_value(),
      static_cast<int>(kTestBidInSellerCurrency *
                           contribution2.value().extended_value().scale() +
                       contribution2.value().extended_value().offset()));
}

TEST(GetPrivateAggregateReportingResponseForWinner,
     PAggReportingResponseWithNoLossContributions) {
  BaseValues base_values = MakeDefaultBaseValues();
  std::vector<PrivateAggregateContribution> win_contributions = {
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "")};
  std::vector<PrivateAggregateContribution> loss_contributions = {
      GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "")};
  std::vector<PrivateAggregateContribution> always_contributions = {
      GetTestContributionWithIntegers(EVENT_TYPE_ALWAYS, "")};
  std::vector<PrivateAggregateContribution> custom_contributions = {
      GetTestContributionWithIntegers(EVENT_TYPE_CUSTOM, kTestCustomEvent)};
  absl::flat_hash_map<std::string, std::vector<PrivateAggregateContribution>>
      custom_event_contributions;
  custom_event_contributions.try_emplace(kTestCustomEvent,
                                         custom_contributions);
  rapidjson::Document doc = CreateContributionResponseDocument(
      win_contributions, loss_contributions, always_contributions,
      custom_event_contributions);
  PrivateAggregateReportingResponse parsed_pagg_response =
      GetPrivateAggregateReportingResponseForWinner(base_values, doc);
  EXPECT_EQ(parsed_pagg_response.contributions().size(),
            3);  // win, always and custom event contributions.
  PrivateAggregateReportingResponse expected_pagg_reporting_response;
  win_contributions[0].clear_event();
  always_contributions[0].clear_event();
  *expected_pagg_reporting_response.add_contributions() = win_contributions[0];
  *expected_pagg_reporting_response.add_contributions() =
      always_contributions[0];
  *expected_pagg_reporting_response.add_contributions() =
      custom_contributions[0];

  google::protobuf::util::MessageDifferencer diff;
  std::string diff_output;
  diff.ReportDifferencesToString(&diff_output);
  EXPECT_TRUE(
      diff.Compare(parsed_pagg_response, expected_pagg_reporting_response))
      << diff_output;
}

TEST(HandlePrivateAggregationReportingTest, ValidLosingAdSuccess) {
  BaseValues base_values = MakeDefaultBaseValues();
  PrivateAggregationHandlerMetadata metadata = {
      .most_desirable_ad_score_id = kTestWinningId.data(),
      .base_values = base_values,
      .ad_id = kTestLossId.data(),
  };

  PrivateAggregateContribution contribution1;
  contribution1.mutable_value()->set_int_value(23);
  contribution1.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);
  contribution1.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);

  std::vector<PrivateAggregateContribution> loss_contributions = {
      contribution1};

  PrivateAggregateContribution contribution2;
  contribution2.mutable_value()->mutable_extended_value()->set_base_value(
      BaseValue::BASE_VALUE_WINNING_BID);
  contribution2.mutable_value()->mutable_extended_value()->set_scale(2.0);
  contribution2.mutable_value()->mutable_extended_value()->set_offset(10);
  contribution2.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);
  contribution2.mutable_bucket()->mutable_bucket_128_bit()->add_bucket_128_bits(
      1);

  std::vector<PrivateAggregateContribution> always_contributions = {
      contribution2};

  ScoreAdsResponse::AdScore score_ads_response;
  HandlePrivateAggregationReporting(
      metadata,
      CreateContributionResponseDocument({}, loss_contributions,
                                         always_contributions, {}),
      score_ads_response);
  EXPECT_EQ(score_ads_response.top_level_contributions(0).contributions_size(),
            2);
  EXPECT_EQ(score_ads_response.top_level_contributions(0)
                .contributions(0)
                .value()
                .int_value(),
            23);
  EXPECT_EQ(
      score_ads_response.top_level_contributions(0)
          .contributions(1)
          .value()
          .int_value(),
      static_cast<int>(kTestBidInSellerCurrency *
                           contribution2.value().extended_value().scale() +
                       contribution2.value().extended_value().offset()));
}

TEST(AppendAdEventContributionsToPaggResponseTest, IntValueWithWinEvent) {
  BaseValues base_values = MakeDefaultBaseValues();

  ValidatePAGGContributions(
      EventType::EVENT_TYPE_WIN,
      R"json({"win": [{"value": {"int_value": 10}}, {"value": {"int_value": 20}}]})json",
      base_values, 2, absl::StatusCode::kOk);
}

TEST(AppendAdEventContributionsToPaggResponseTest, ExtendedValueWithWinEvent) {
  BaseValues base_values = MakeDefaultBaseValues();

  ValidatePAGGContributions(
      EventType::EVENT_TYPE_WIN,
      R"json({"win": [{"value": {"extended_value": {"base_value": "BASE_VALUE_WINNING_BID", "offset": 1, "scale": 2.0}}}, {"value": {"extended_value": {"base_value": "BASE_VALUE_HIGHEST_SCORING_OTHER_BID", "offset": 1, "scale": 2.0}}}]})json",
      base_values, 2, absl::StatusCode::kOk);
}

TEST(AppendAdEventContributionsToPaggResponseTest, UnsupportedBaseValue) {
  BaseValues base_values = MakeDefaultBaseValues();

  // Expected contribution count is 0
  // because BASE_VALUE_SCRIPT_RUN_TIME is not yet supported.
  ValidatePAGGContributions(
      EventType::EVENT_TYPE_WIN,
      R"json({"win": [{"value": {"extended_value": {"base_value": "BASE_VALUE_SCRIPT_RUN_TIME", "offset": 1, "scale": 2.0}}}]})json",
      base_values, 0, absl::StatusCode::kOk);
}

TEST(AppendAdEventContributionsToPaggResponseTest, InvalidEventType) {
  BaseValues base_values = MakeDefaultBaseValues();

  ValidatePAGGContributions(
      EventType::EVENT_TYPE_UNSPECIFIED,
      R"json({"win": [{"value": {"int_value": 10}}]})json", base_values, 0,
      absl::StatusCode::kInvalidArgument);
}

TEST(AppendAdEventContributionsToPaggResponseTest, CustomEventType) {
  BaseValues base_values = MakeDefaultBaseValues();

  ValidatePAGGContributions(
      EventType::EVENT_TYPE_CUSTOM,
      R"json({"custom_events": {"event1": [{"value": {"int_value": 10}}], "event2": [{"value": {"int_value": 20}}] }})json",
      base_values, 2, absl::StatusCode::kOk);
}

TEST(AppendAdEventContributionsToPaggResponseTest,
     CustomEventTypeWithInvalidEvent) {
  BaseValues base_values = MakeDefaultBaseValues();

  ValidatePAGGContributions(
      EventType::EVENT_TYPE_CUSTOM,
      R"json({"custom_events": {"event1": [{"value": {"int_value": 10}}], "event2": 20}})json",
      base_values, 1, absl::StatusCode::kOk);
}

TEST(AppendAdEventContributionsToPaggResponseTest, MissingSpecifiedEventType) {
  BaseValues base_values = MakeDefaultBaseValues();

  ValidatePAGGContributions(EventType::EVENT_TYPE_WIN, R"json({})json",
                            base_values, 0, absl::StatusCode::kInternal);
}

TEST(AppendAdEventContributionsToPaggResponseTest,
     RejectionReasonAsBaseValueButNoRejectionReason) {
  BaseValues base_values = MakeDefaultBaseValuesWithNoRejectionReason();

  ValidatePAGGContributions(
      EventType::EVENT_TYPE_WIN,
      R"json({"win": [{"value": {"extended_value": {"base_value": "BASE_VALUE_BID_REJECTION_REASON", "offset": 1, "scale": 2.0}}}]})json",
      base_values, 0, absl::StatusCode::kOk);
}

TEST(ParseAndProcessContributionTest,
     PAggValueAsIntValueAndBucketAsSignalBucketSuccess) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"int_value": 10}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 10, /* expected lower bits*/ 1,
      /* expected upper bits */ 2, absl::StatusCode::kOk);
}

TEST(ParseAndProcessContributionTest, PAggValueAsExtendedValueSuccess) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_WINNING_BID", "offset": 1, "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kOk);
}

TEST(ParseAndProcessContributionTest,
     PAggValueWithInvalidBaseValueReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_SCRIPT_RUN_TIME", "offset": 1, "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     ContributionWithInvalidValueFieldNameReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"invalid_field_name": {"base_value": "BASE_VALUE_WINNING_BID", "offset": 1, "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     PAggValueExtendedValueMissingBaseValueReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"offset": 1, "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     PAggValueExtendedValueMissingOffsetReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_WINNING_BID", "offset": "invalid_offset", "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     PAggValueExtendedValueMissingScaleReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_WINNING_BID", "offset": 1}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     PAggValueExtendedValueInvalidOffsetFieldReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_WINNING_BID", "offset": "invalid_offset_field", "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     PAggValueExtendedValueInvalidScaleFieldReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValues();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_WINNING_BID", "offset": 1, "scale": "invalid_scale_field"}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(ParseAndProcessContributionTest,
     PAggValueRejectionReasonAsBaseValueWithoutOneReturnInvalidArgumentError) {
  BaseValues base_values = MakeDefaultBaseValuesWithNoRejectionReason();

  TestParseAndProcessContribution(
      /* base_values */ base_values,
      /* json_string */
      R"json({"value": {"extended_value": {"base_value": "BASE_VALUE_BID_REJECTION_REASON", "offset": 1, "scale": 2.0}}, "bucket": {"bucket_128_bit":  {"bucket_128_bits": [1, 2]}}})json",
      /* expected_int_value */ 21,
      /* expected lower bits*/ 1, /* expected upper bits */ 2,
      absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParsePrivateAggregationSignalValueSuccess) {
  TestParseSignalValue(
      R"JSON({"base_value": "BASE_VALUE_WINNING_BID","offset": 10,"scale": 2.5})JSON",
      BaseValue::BASE_VALUE_WINNING_BID, 10, 2.5, absl::StatusCode::kOk);
}

TEST(PrivateAggregationManagerTest,
     ParsePrivateAggregationSignalValueMissingBaseValue) {
  TestParseSignalValue(R"JSON({"offset": 10,"scale": 2.5})JSON",
                       BaseValue::BASE_VALUE_UNSPECIFIED, 0, 0,
                       absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest,
     ParsePrivateAggregationSignalValueMissingOffset) {
  TestParseSignalValue(
      R"JSON({"base_value": "BASE_VALUE_WINNING_BID","scale": 2.5})JSON",
      BaseValue::BASE_VALUE_UNSPECIFIED, 0, 0,
      absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest,
     ParsePrivateAggregationSignalValueMissingScale) {
  TestParseSignalValue(
      R"JSON({"base_value": "BASE_VALUE_WINNING_BID","offset": 10})JSON",
      BaseValue::BASE_VALUE_UNSPECIFIED, 0, 0,
      absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParseSignalBucketSuccess) {
  TestParseSignalBucket(
      R"json({"offset": {"value": [1, 2],"is_negative": true}, "base_value": "BASE_VALUE_WINNING_BID","scale": 2.5})json",
      BaseValue::BASE_VALUE_WINNING_BID, 1, 2, true, 2.5,
      absl::StatusCode::kOk);
}

TEST(PrivateAggregationManagerTest, ParseSignalBucketMissingOffset) {
  TestParseSignalBucket(
      R"json({"base_value": "BASE_VALUE_WINNING_BID","scale": 2.5})json",
      BaseValue::BASE_VALUE_UNSPECIFIED, 0, 0, false, 0,
      absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParseSignalBucketMissingBaseValue) {
  TestParseSignalBucket(
      R"json({"offset": {"value": [1, 2],"is_negative": true}, "scale": 2.5})json",
      BaseValue::BASE_VALUE_UNSPECIFIED, 0, 0, false, 0,
      absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParseSignalBucketMissingScale) {
  TestParseSignalBucket(
      R"json({"offset": {"value": [1, 2],"is_negative": true}, "base_value": "BASE_VALUE_WINNING_BID"})json",
      BaseValue::BASE_VALUE_UNSPECIFIED, 0, 0, false, 0,
      absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParseEmptySignalBucket) {
  TestParseSignalBucket(R"json({})json", BaseValue::BASE_VALUE_UNSPECIFIED, 0,
                        0, false, 0, absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParseBucketOffsetSuccess) {
  TestParseBucketOffset(R"json({"value": [1, 2],"is_negative": true})json", 1,
                        2, true, absl::StatusCode::kOk);
}

TEST(PrivateAggregationManagerTest, ParseBucketOffsetMissingIsNegative) {
  TestParseBucketOffset(R"json({"value": [1, 2]})json", 1, 2, false,
                        absl::StatusCode::kInvalidArgument);
}

TEST(PrivateAggregationManagerTest, ParseEmptyBucketOffset) {
  TestParseBucketOffset(R"json({})json", 1, 2, false,
                        absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
