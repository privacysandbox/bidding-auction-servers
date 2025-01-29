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

#include "services/auction_service/private_aggregation/private_aggregation_manager_test_util.h"

#include "gtest/gtest.h"
#include "include/rapidjson/document.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

// Converts a PrivateAggregateContribution object to its JSON representation.
rapidjson::Value ContributionToJson(
    const PrivateAggregateContribution& contribution,
    rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value contribution_json(rapidjson::kObjectType);
  rapidjson::Value value_json(rapidjson::kObjectType);

  if (contribution.has_value()) {
    if (contribution.value().has_int_value()) {
      value_json.AddMember(rapidjson::StringRef(kSignalValueIntValue,
                                                strlen(kSignalValueIntValue)),
                           rapidjson::Value(contribution.value().int_value()),
                           allocator);
    } else if (contribution.value().has_extended_value()) {
      rapidjson::Value signal_value = CreateSignalValue(
          BaseValue_Name(contribution.value().extended_value().base_value()),
          contribution.value().extended_value().offset(),
          contribution.value().extended_value().scale(), allocator);
      value_json.AddMember(
          rapidjson::StringRef(kSignalValueExtendedValue,
                               strlen(kSignalValueExtendedValue)),
          signal_value, allocator);
    }
  }
  contribution_json.AddMember(
      rapidjson::StringRef(kPrivateAggregationValue.data(),
                           kPrivateAggregationValue.length()),
      value_json, allocator);

  // Add bucket information if available.
  if (contribution.has_bucket()) {
    rapidjson::Value bucket_json(rapidjson::kObjectType);
    if (contribution.bucket().has_bucket_128_bit()) {
      rapidjson::Value bucket_128_bit_json(rapidjson::kObjectType);
      rapidjson::Value bucket_128_bits_json(rapidjson::kArrayType);
      for (const auto& bucket_value :
           contribution.bucket().bucket_128_bit().bucket_128_bits()) {
        bucket_128_bits_json.PushBack(bucket_value, allocator);
      }
      bucket_128_bit_json.AddMember(
          rapidjson::StringRef(kSignalBucketBucket128BitBucket128Bits,
                               strlen(kSignalBucketBucket128BitBucket128Bits)),
          bucket_128_bits_json, allocator);

      bucket_json.AddMember(
          rapidjson::StringRef(kSignalBucketBucket128Bit,
                               strlen(kSignalBucketBucket128Bit)),
          bucket_128_bit_json, allocator);
    }
    contribution_json.AddMember(
        rapidjson::StringRef(kPrivateAggregationBucket.data(),
                             kPrivateAggregationBucket.length()),
        bucket_json, allocator);
  }
  return contribution_json;
}

}  // namespace

void ValidatePAGGContributions(EventType event_type,
                               const std::string& pagg_response_json_string,
                               BaseValues& base_values,
                               int expected_contribution_count,
                               absl::StatusCode expected_status_code) {
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(pagg_response_json_string.c_str());
  ASSERT_TRUE(document.ok());
  PrivateAggregateReportingResponse pagg_response;
  absl::Status status = AppendAdEventContributionsToPaggResponse(
      event_type, document.value(), base_values, pagg_response);
  ASSERT_EQ(status.code(), expected_status_code);
  // Exit early if the status code is not kOk.
  // Only check value if status is ok.
  if (status.ok()) {
    EXPECT_EQ(pagg_response.contributions_size(), expected_contribution_count);
  }
}

void TestParseAndProcessContribution(const BaseValues& base_values,
                                     const std::string& json_string,
                                     int expected_int_value,
                                     uint64_t expected_lower_bits,
                                     uint64_t expected_upper_bits,
                                     absl::StatusCode expected_status_code) {
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(json_string.c_str());
  ASSERT_TRUE(document.ok());
  absl::StatusOr<PrivateAggregateContribution> contribution =
      ParseAndProcessContribution(base_values, document.value());
  ASSERT_EQ(contribution.status().code(), expected_status_code);
  // Exit early if the status code is not Ok.
  if (expected_status_code != absl::StatusCode::kOk) {
    return;
  }
  EXPECT_EQ(contribution.value().value().int_value(), expected_int_value);
  EXPECT_FALSE(contribution.value().value().has_extended_value());

  EXPECT_EQ(contribution.value().bucket().bucket_128_bit().bucket_128_bits(0),
            expected_lower_bits);
  EXPECT_EQ(contribution.value().bucket().bucket_128_bit().bucket_128_bits(1),
            expected_upper_bits);
}

void TestParseSignalValue(const std::string& json_string,
                          BaseValue expected_base_value, int expected_offset,
                          double expected_scale,
                          absl::StatusCode expected_status_code) {
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(json_string.c_str());
  ASSERT_TRUE(document.ok());
  absl::StatusOr<SignalValue> signal_value = ParseSignalValue(document.value());
  ASSERT_EQ(signal_value.status().code(), expected_status_code);
  // Exit early if the status code is not Ok.
  if (expected_status_code != absl::StatusCode::kOk) {
    return;
  }
  EXPECT_EQ(signal_value.value().base_value(), expected_base_value);
  EXPECT_EQ(signal_value.value().offset(), expected_offset);
  EXPECT_EQ(signal_value.value().scale(), expected_scale);
}

void TestParseSignalBucket(const std::string& json_string,
                           BaseValue expected_base_value,
                           int64_t expected_offset_0, int64_t expected_offset_1,
                           bool expected_is_negative, double expected_scale,
                           absl::StatusCode expected_status_code) {
  absl::StatusOr<rapidjson::Document> document =
      ParseJsonString(json_string.c_str());
  ASSERT_TRUE(document.ok());
  absl::StatusOr<SignalBucket> signal_bucket =
      ParseSignalBucket(document.value());
  ASSERT_EQ(signal_bucket.status().code(), expected_status_code);

  // Exit early if the status code is not Ok.
  if (expected_status_code != absl::StatusCode::kOk) {
    return;
  }

  EXPECT_EQ(signal_bucket.value().base_value(), expected_base_value);
  EXPECT_EQ(signal_bucket.value().offset().value()[0], expected_offset_0);
  EXPECT_EQ(signal_bucket.value().offset().value()[1], expected_offset_1);
  EXPECT_EQ(signal_bucket.value().offset().is_negative(), expected_is_negative);
  EXPECT_EQ(signal_bucket.value().scale(), expected_scale);
}

void TestParseBucketOffset(absl::string_view json_string,
                           int64_t expected_value_0, int64_t expected_value_1,
                           bool expected_is_negative,
                           absl::StatusCode expected_status_code) {
  absl::StatusOr<rapidjson::Document> document = ParseJsonString(json_string);
  absl::StatusOr<BucketOffset> bucket_offset =
      ParseBucketOffset(document.value());
  EXPECT_EQ(bucket_offset.status().code(), expected_status_code);

  // Exit early if the status code is not Ok.
  if (expected_status_code != absl::StatusCode::kOk) {
    return;
  }

  EXPECT_EQ(bucket_offset.value().value().size(), 2);
  EXPECT_EQ(bucket_offset.value().value()[0], expected_value_0);
  EXPECT_EQ(bucket_offset.value().value()[1], expected_value_1);
  EXPECT_EQ(bucket_offset.value().is_negative(), expected_is_negative);
}

rapidjson::Value CreateSignalValue(
    absl::string_view base_value, int offset, double scale,
    rapidjson::Document::AllocatorType& allocator) {
  rapidjson::Value signal_value(rapidjson::kObjectType);

  signal_value.AddMember(
      rapidjson::StringRef(kSignalValueBaseValue.data(),
                           kSignalValueBaseValue.length()),
      rapidjson::Value(kBaseValueWinningBid.data(), allocator), allocator);
  signal_value.AddMember(rapidjson::StringRef(kSignalValueOffset.data(),
                                              kSignalValueOffset.length()),
                         rapidjson::Value(offset), allocator);
  signal_value.AddMember(rapidjson::StringRef(kSignalValueScale.data(),
                                              kSignalValueScale.length()),
                         rapidjson::Value(scale), allocator);
  return signal_value;
}

rapidjson::Document CreateContributionResponseDocument(
    const std::vector<PrivateAggregateContribution>& win_contributions,
    const std::vector<PrivateAggregateContribution>& loss_contributions,
    const std::vector<PrivateAggregateContribution>& always_contributions,
    const absl::flat_hash_map<std::string,
                              std::vector<PrivateAggregateContribution>>&
        custom_event_contributions) {
  rapidjson::Document doc(rapidjson::kObjectType);
  auto& allocator = doc.GetAllocator();
  auto addContributionsToJson =
      [&doc, &allocator](
          const absl::string_view event_type,
          const std::vector<PrivateAggregateContribution>& contributions) {
        rapidjson::Value contribution_array(rapidjson::kArrayType);
        for (const auto& contribution : contributions) {
          contribution_array.PushBack(
              ContributionToJson(contribution, allocator), allocator);
        }
        doc.AddMember(
            rapidjson::StringRef(event_type.data(), event_type.length()),
            contribution_array, allocator);
      };

  addContributionsToJson(kEventTypeWin, win_contributions);
  addContributionsToJson(kEventTypeLoss, loss_contributions);
  addContributionsToJson(kEventTypeAlways, always_contributions);

  if (!custom_event_contributions.empty()) {
    rapidjson::Value custom_events(rapidjson::kObjectType);
    for (const auto& [event_name, contributions] : custom_event_contributions) {
      rapidjson::Value contribution_array(rapidjson::kArrayType);
      for (const auto& contribution : contributions) {
        contribution_array.PushBack(ContributionToJson(contribution, allocator),
                                    allocator);
      }
      custom_events.AddMember(
          rapidjson::StringRef(event_name.c_str(), event_name.length()),
          contribution_array, allocator);
    }
    doc.AddMember(rapidjson::StringRef(kEventTypeCustom.data(),
                                       kEventTypeCustom.length()),
                  custom_events, allocator);
  }
  return doc;
}

BaseValues MakeDefaultBaseValues() {
  BaseValues base_values;
  base_values.winning_bid = kTestBidInSellerCurrency;
  base_values.highest_scoring_other_bid = kTestHighestScoringOtherBidValue;
  base_values.reject_reason = SellerRejectionReason::INVALID_BID;
  return base_values;
}

BaseValues MakeDefaultBaseValuesWithNoRejectionReason() {
  BaseValues base_values;
  base_values.winning_bid = kTestBidInSellerCurrency;
  base_values.highest_scoring_other_bid = kTestHighestScoringOtherBidValue;
  return base_values;
}

}  // namespace privacy_sandbox::bidding_auction_servers
