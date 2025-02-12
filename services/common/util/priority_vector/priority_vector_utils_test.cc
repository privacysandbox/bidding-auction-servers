// Copyright 2024 Google LLC
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

#include "services/common/util/priority_vector/priority_vector_utils.h"

#include <string>
#include <utility>

#include <include/gmock/gmock-actions.h>

#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using PerBuyerConfigMap =
    google::protobuf::Map<std::string,
                          SelectAdRequest::AuctionConfig::PerBuyerConfig>;

inline constexpr char kBuyerName[] = "buyer_name";

class PriorityVectorUtilsTest : public testing::Test {
 protected:
  void SetUp() override { CommonTestInit(); }
};

PerBuyerConfigMap CreatePerBuyerConfig(
    const std::string& buyer_name,
    const rapidjson::Document& priority_signals_vector_overrides) {
  SelectAdRequest::AuctionConfig::PerBuyerConfig buyer_config;

  absl::StatusOr<std::string> serialized_json =
      SerializeJsonDoc(priority_signals_vector_overrides);
  CHECK_OK(serialized_json);
  buyer_config.set_priority_signals_overrides(*serialized_json);

  PerBuyerConfigMap per_buyer_config;
  per_buyer_config[buyer_name] = std::move(buyer_config);

  return per_buyer_config;
}

BuyerInputForBidding::InterestGroupForBidding CreateInterestGroup(
    const std::string& name, absl::Duration ig_age) {
  BrowserSignalsForBidding browser_signals;
  browser_signals.set_recency_ms(absl::ToInt64Milliseconds(ig_age));

  BuyerInputForBidding::InterestGroupForBidding interest_group;
  interest_group.set_name(name);
  *interest_group.mutable_browser_signals() = std::move(browser_signals);

  return interest_group;
}

TEST_F(PriorityVectorUtilsTest, ParsePriorityVectorTest) {
  rapidjson::Document document(rapidjson::kObjectType);
  document.AddMember("a", "a string value", document.GetAllocator());
  document.AddMember("b", 1, document.GetAllocator());
  document.AddMember("c", 10.0, document.GetAllocator());
  document.AddMember("d", "100", document.GetAllocator());
  absl::StatusOr<std::string> serialized_json = SerializeJsonDoc(document);
  ASSERT_TRUE(serialized_json.ok()) << serialized_json.status();

  absl::StatusOr<rapidjson::Document> priority_vector_doc =
      ParsePriorityVector(*serialized_json);
  ASSERT_TRUE(priority_vector_doc.ok()) << priority_vector_doc.status();
  EXPECT_EQ(priority_vector_doc->MemberCount(), 2);
  EXPECT_EQ((*priority_vector_doc)["b"].GetDouble(), 1.0)
      << absl::StrCat("Actual: ", (*priority_vector_doc)["b"].GetDouble());
  EXPECT_EQ((*priority_vector_doc)["c"].GetDouble(), 10.0)
      << absl::StrCat("Actual: ", (*priority_vector_doc)["c"].GetDouble());
}

TEST_F(PriorityVectorUtilsTest, SanitizePriorityVectorTest) {
  rapidjson::Document priority_signals(rapidjson::kObjectType);
  priority_signals.AddMember("a", "a string value",
                             priority_signals.GetAllocator());
  priority_signals.AddMember("b", 5, priority_signals.GetAllocator());
  priority_signals.AddMember("d", "100", priority_signals.GetAllocator());
  SanitizePriorityVector(priority_signals);

  EXPECT_EQ(priority_signals.MemberCount(), 1)
      << absl::StrCat("Actual: ", priority_signals.MemberCount());
  EXPECT_EQ(priority_signals["b"].GetDouble(), 5.0)
      << absl::StrCat("Actual: ", priority_signals["b"].GetDouble());
}

TEST_F(PriorityVectorUtilsTest,
       GetBuyerPrioritySignals_VerifyExistingAndOverridenValues) {
  rapidjson::Document priority_signals_vector(rapidjson::kObjectType);
  priority_signals_vector.AddMember("a", 1,
                                    priority_signals_vector.GetAllocator());
  priority_signals_vector.AddMember("b", 10,
                                    priority_signals_vector.GetAllocator());

  rapidjson::Document priority_signals_vector_overrides(rapidjson::kObjectType);
  priority_signals_vector_overrides.AddMember(
      "b", 50, priority_signals_vector_overrides.GetAllocator());
  priority_signals_vector_overrides.AddMember(
      "c", 100, priority_signals_vector_overrides.GetAllocator());

  PerBuyerConfigMap per_buyer_config =
      CreatePerBuyerConfig(kBuyerName, priority_signals_vector_overrides);
  absl::StatusOr<std::string> result_str = GetBuyerPrioritySignals(
      priority_signals_vector, per_buyer_config, kBuyerName);
  ASSERT_TRUE(result_str.ok()) << result_str.status();
  absl::StatusOr<rapidjson::Document> result_doc = ParseJsonString(*result_str);
  ASSERT_TRUE(result_doc.ok()) << result_doc.status();
  EXPECT_EQ((*result_doc)["a"].GetDouble(), 1)
      << absl::StrCat("Actual: ", (*result_doc)["a"].GetDouble());
  EXPECT_EQ((*result_doc)["b"].GetDouble(), 50)
      << absl::StrCat("Actual: ", (*result_doc)["b"].GetDouble());
  EXPECT_EQ((*result_doc)["c"].GetDouble(), 100)
      << absl::StrCat("Actual: ", (*result_doc)["c"].GetDouble());
}

TEST_F(PriorityVectorUtilsTest,
       GetBuyerPrioritySignals_VerifyIncorrectEntryTypesAreIgnored) {
  rapidjson::Document priority_signals_vector(rapidjson::kObjectType);

  priority_signals_vector.AddMember("a", 1,
                                    priority_signals_vector.GetAllocator());

  rapidjson::Document priority_signals_vector_overrides(rapidjson::kObjectType);
  priority_signals_vector_overrides.AddMember(
      "a", "a string value", priority_signals_vector_overrides.GetAllocator());

  PerBuyerConfigMap per_buyer_config =
      CreatePerBuyerConfig(kBuyerName, priority_signals_vector_overrides);
  absl::StatusOr<std::string> result_str = GetBuyerPrioritySignals(
      priority_signals_vector, per_buyer_config, kBuyerName);
  ASSERT_TRUE(result_str.ok()) << result_str.status();
  absl::StatusOr<rapidjson::Document> result_doc = ParseJsonString(*result_str);
  ASSERT_TRUE(result_doc.ok()) << result_doc.status();
  EXPECT_EQ((*result_doc)["a"].GetDouble(), 1)
      << absl::StrCat("Actual: ", (*result_doc)["a"].GetDouble());
}

TEST_F(PriorityVectorUtilsTest,
       CalculateInterestGroupPrioritiesTest_VerifyDeviceSignalFields) {
  rapidjson::Document priority_signals(rapidjson::kObjectType);
  priority_signals.AddMember("a", 1, priority_signals.GetAllocator());

  absl::flat_hash_map<std::string, rapidjson::Value> per_ig_priority_vectors;

  rapidjson::Document priority_vector_doc;
  rapidjson::Value priority_vector(rapidjson::kObjectType);
  priority_vector.AddMember("a", -1, priority_vector_doc.GetAllocator());
  priority_vector.AddMember("deviceSignals.one", 5,
                            priority_vector_doc.GetAllocator());
  priority_vector.AddMember("deviceSignals.ageInMinutes", -10,
                            priority_vector_doc.GetAllocator());
  priority_vector.AddMember("deviceSignals.ageInMinutesMax60", 15,
                            priority_vector_doc.GetAllocator());
  priority_vector.AddMember("deviceSignals.ageInHoursMax24", -20,
                            priority_vector_doc.GetAllocator());
  priority_vector.AddMember("deviceSignals.ageInDaysMax30", 25,
                            priority_vector_doc.GetAllocator());

  absl::Duration ig_age = absl::Minutes(30);
  BuyerInputForBidding::InterestGroupForBidding interest_group =
      CreateInterestGroup("ig_name", ig_age);

  rapidjson::Value priority_vector_copy(rapidjson::kObjectType);
  priority_vector_copy.CopyFrom(priority_vector,
                                priority_vector_doc.GetAllocator());
  per_ig_priority_vectors[interest_group.name()] =
      std::move(priority_vector_copy);

  BuyerInputForBidding buyer_input;
  *buyer_input.mutable_interest_groups()->Add() = std::move(interest_group);

  double expected_priority =
      (priority_vector["a"].GetDouble() * priority_signals["a"].GetDouble()) +
      (priority_vector["deviceSignals.one"].GetDouble() * 1) +
      (priority_vector["deviceSignals.ageInMinutes"].GetDouble() *
       absl::ToDoubleMinutes(ig_age)) +
      (priority_vector["deviceSignals.ageInMinutesMax60"].GetDouble() *
       absl::ToDoubleMinutes(ig_age)) +
      (priority_vector["deviceSignals.ageInHoursMax24"].GetDouble() *
       absl::ToDoubleHours(ig_age)) +
      (priority_vector["deviceSignals.ageInDaysMax30"].GetDouble() *
       (absl::ToDoubleHours(ig_age) / 24));

  absl::flat_hash_map<std::string, double> per_ig_priorities =
      CalculateInterestGroupPriorities(priority_signals, buyer_input,
                                       per_ig_priority_vectors);
  EXPECT_EQ(expected_priority, per_ig_priorities["ig_name"]);
  EXPECT_EQ(priority_signals.MemberCount(), 1);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
