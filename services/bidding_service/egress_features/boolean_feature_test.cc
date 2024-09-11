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

#include "services/bidding_service/egress_features/boolean_feature.h"

#include <utility>

#include <include/gmock/gmock-matchers.h>

#include "absl/log/check.h"
#include "absl/strings/substitute.h"
#include "gtest/gtest.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::testing::HasSubstr;

class BooleanFeatureTest : public ::testing::Test {
 protected:
  rapidjson::Value TestValue(bool value) {
    std::string test_schema = absl::Substitute(R"JSON(
      {
        "test_wrapper": {
          "name": "boolean-feature",
          "value": $0
        }
      }
      )JSON",
                                               value);
    auto parsed_doc = ParseJsonString(test_schema);
    CHECK_OK(parsed_doc);
    test_value_doc_ = *std::move(parsed_doc);
    rapidjson::Value test_value;
    PS_ASSIGN_IF_PRESENT(test_value, test_value_doc_, "test_wrapper", Object);
    return test_value;
  }

  rapidjson::Document test_value_doc_;
};

TEST_F(BooleanFeatureTest, ReturnsDefaultIfValueNotSetPreviously) {
  BooleanFeature bool_feature(/*size=*/1);
  auto serialized_val = bool_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_val(1, false);
  EXPECT_EQ(*serialized_val, expected_val);
}

TEST_F(BooleanFeatureTest, SerializesTrueCorrectly) {
  BooleanFeature bool_feature(/*size*/ 1);
  auto status = bool_feature.SetValue(TestValue(true));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = bool_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{true};
  EXPECT_EQ(*serialized_val, expected_result);
}

TEST_F(BooleanFeatureTest, SerializesFalseCorrectly) {
  BooleanFeature bool_feature(/*size*/ 1);
  auto status = bool_feature.SetValue(TestValue(false));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = bool_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{false};
  EXPECT_EQ(*serialized_val, expected_result);
}

TEST_F(BooleanFeatureTest, VerifyType) {
  BooleanFeature bool_feature(/*size*/ 1);
  EXPECT_EQ(bool_feature.Type(), "boolean-feature");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
