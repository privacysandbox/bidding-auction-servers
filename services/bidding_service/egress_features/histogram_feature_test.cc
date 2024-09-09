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

#include "services/bidding_service/egress_features/histogram_feature.h"

#include <utility>

#include <include/gmock/gmock-matchers.h>

#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "gtest/gtest.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
using ::testing::HasSubstr;

using FeatureArray =
    std::vector<std::pair</*type=*/std::string, /*value=*/int>>;

class HistogramFeatureTest : public ::testing::Test {
 protected:
  std::shared_ptr<rapidjson::Value> TestSchema() {
    std::string test_schema = R"JSON(
      {
        "name": "histogram-feature",
        "test_wrapper": {
          "value": [
            {
              "name": "unsigned-integer-feature",
              "size": 2
            },
            {
              "name": "signed-integer-feature",
              "size": 3
            }
          ]}
      }
      )JSON";
    auto parsed_doc = ParseJsonString(test_schema);
    CHECK_OK(parsed_doc);
    test_schema_doc_ = *std::move(parsed_doc);
    auto test_value = std::make_shared<rapidjson::Value>();
    PS_ASSIGN_IF_PRESENT(*test_value, test_schema_doc_, "test_wrapper", Object);
    return test_value;
  }

  rapidjson::Value TestValue(const FeatureArray& num_features) {
    std::string histogram_feat_json = absl::StrJoin(
        num_features, ",",
        [](std::string* out, const std::pair<std::string, int>& type_feat) {
          absl::StrAppend(out,
                          absl::Substitute(R"JSON(
              {
                "name": "$0",
                "value": $1
              }
        )JSON",
                                           type_feat.first, type_feat.second));
        });
    std::string test_schema = absl::Substitute(R"JSON(
      {
        "test_wrapper": {"name": "histogram-feature", "value": [ $0 ]}
      }
      )JSON",
                                               histogram_feat_json);
    auto parsed_doc = ParseJsonString(test_schema);
    CHECK_OK(parsed_doc);
    test_value_doc_ = *std::move(parsed_doc);
    rapidjson::Value test_value;
    PS_ASSIGN_IF_PRESENT(test_value, test_value_doc_, "test_wrapper", Object);
    return test_value;
  }

  rapidjson::Document test_schema_doc_;
  rapidjson::Document test_value_doc_;
};

TEST_F(HistogramFeatureTest, ReturnsDefaultIfValueNotSetPreviously) {
  HistogramFeature histogram_feature(/*size=*/2, TestSchema());
  auto serialized_val = histogram_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_val(histogram_feature.Size(), false);
  EXPECT_EQ(*serialized_val, expected_val);
}

TEST_F(HistogramFeatureTest, ComplainsAboutSize) {
  HistogramFeature histogram_feature(/*size=*/2, TestSchema());
  auto status = histogram_feature.SetValue(
      TestValue(FeatureArray({{"unsigned-integer-feature", 1},
                              {"signed-integer-feature", 2},
                              {"unsigned-integer-feature", 3}})));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = histogram_feature.Serialize();
  ASSERT_TRUE(!serialized_val.ok()) << serialized_val.status();
  EXPECT_THAT(serialized_val.status().message(),
              HasSubstr("Number of buckets in histogram feature payload (3) "
                        "doesn't match schema (2)"));
}

TEST_F(HistogramFeatureTest, SerializesMultipleBuckets) {
  HistogramFeature histogram_feature(/*size=*/2, TestSchema());
  auto status = histogram_feature.SetValue(TestValue(FeatureArray(
      {{"unsigned-integer-feature", 2}, {"signed-integer-feature", -3}})));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = histogram_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{true, false, true, true, false};
  EXPECT_EQ(*serialized_val, expected_result);
}

TEST_F(HistogramFeatureTest, VerifyType) {
  HistogramFeature histogram_feature(/*size=*/2, TestSchema());
  EXPECT_EQ(histogram_feature.Type(), "histogram-feature");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
