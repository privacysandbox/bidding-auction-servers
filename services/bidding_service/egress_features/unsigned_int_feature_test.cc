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

#include "services/bidding_service/egress_features/unsigned_int_feature.h"

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

class UnsignedIntFeatureTest : public ::testing::Test {
 protected:
  rapidjson::Value TestValue(int value) {
    std::string test_schema = absl::Substitute(R"JSON(
      {
        "test_wrapper": {
          "name": "unsigned-integer-feature",
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

TEST_F(UnsignedIntFeatureTest, ReturnsDefaultIfValueNotSetPreviously) {
  UnsignedIntFeature unsigned_int_feature(/*size*/ 3);
  auto serialized_val = unsigned_int_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_val(3, false);
  EXPECT_EQ(*serialized_val, expected_val);
}

TEST_F(UnsignedIntFeatureTest, ComplainsForOutOfBoundNumber) {
  UnsignedIntFeature unsigned_int_feature(/*size*/ 3);
  auto status = unsigned_int_feature.SetValue(TestValue(8));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = unsigned_int_feature.Serialize();
  ASSERT_TRUE(!serialized_val.ok()) << serialized_val.status();
  EXPECT_THAT(serialized_val.status().message(),
              HasSubstr("Out of bound error"));
}

TEST_F(UnsignedIntFeatureTest, MaxValueIsOk) {
  UnsignedIntFeature unsigned_int_feature(/*size*/ 3);
  auto status = unsigned_int_feature.SetValue(TestValue(7));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = unsigned_int_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{true, true, true};
  EXPECT_EQ(*serialized_val, expected_result);
}

TEST_F(UnsignedIntFeatureTest, CorrectlySerializesZero) {
  UnsignedIntFeature unsigned_int_feature(/*size*/ 3);
  auto status = unsigned_int_feature.SetValue(TestValue(0));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = unsigned_int_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{false, false, false};
  EXPECT_EQ(*serialized_val, expected_result);
}

TEST_F(UnsignedIntFeatureTest, VerifyType) {
  UnsignedIntFeature unsigned_int_feature(/*size*/ 3);
  EXPECT_EQ(unsigned_int_feature.Type(), "unsigned-integer-feature");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
