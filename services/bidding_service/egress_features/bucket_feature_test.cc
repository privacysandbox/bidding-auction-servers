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

#include "services/bidding_service/egress_features/bucket_feature.h"

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

class BucketFeatureTest : public ::testing::Test {
 protected:
  rapidjson::Value TestValue(const std::vector<bool>& bool_features) {
    std::string bool_feat_json = absl::StrJoin(
        bool_features, ",", [](std::string* out, const bool feat) {
          absl::StrAppend(out, absl::Substitute(R"JSON(
              {
                "value": $0
              }
        )JSON",
                                                feat));
        });
    std::string test_schema = absl::Substitute(R"JSON(
      {
        "test_wrapper": {"name": "bucket-feature", "value": [ $0 ]}
      }
      )JSON",
                                               bool_feat_json);
    auto parsed_doc = ParseJsonString(test_schema);
    CHECK_OK(parsed_doc);
    test_value_doc_ = *std::move(parsed_doc);
    rapidjson::Value test_value;
    PS_ASSIGN_IF_PRESENT(test_value, test_value_doc_, "test_wrapper", Object);
    return test_value;
  }

  rapidjson::Document test_value_doc_;
};

TEST_F(BucketFeatureTest, ReturnsDefaultIfValueNotSetPreviously) {
  BucketFeature bucket_feature(/*size=*/3);
  auto serialized_val = bucket_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_val(3, false);
  EXPECT_EQ(*serialized_val, expected_val);
}

TEST_F(BucketFeatureTest, VerifyTye) {
  BucketFeature bucket_feature(/*size=*/1);
  EXPECT_EQ(bucket_feature.Type(), "bucket-feature");
}

TEST_F(BucketFeatureTest, ComplainsAboutSizeMismatch) {
  BucketFeature bucket_feature(/*size=*/1);
  auto status =
      bucket_feature.SetValue(TestValue(std::vector<bool>({true, false})));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = bucket_feature.Serialize();
  ASSERT_TRUE(!serialized_val.ok()) << serialized_val.status();
  EXPECT_THAT(serialized_val.status().message(),
              HasSubstr("Number of buckets in feature payload (2) doesn't "
                        "match schema (1)"));
}

TEST_F(BucketFeatureTest, SerializesASingleBucket) {
  BucketFeature bucket_feature(/*size=*/1);
  auto status = bucket_feature.SetValue(TestValue(std::vector<bool>({true})));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = bucket_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{true};
  EXPECT_EQ(*serialized_val, expected_result);
}

TEST_F(BucketFeatureTest, SerializesMultipleBuckets) {
  BucketFeature bucket_feature(/*size=*/2);
  auto status =
      bucket_feature.SetValue(TestValue(std::vector<bool>({true, false})));
  // We only lazily verify the value fitting in the allowed size when we
  // serialize.
  CHECK_OK(status);
  auto serialized_val = bucket_feature.Serialize();
  ASSERT_TRUE(serialized_val.ok()) << serialized_val.status();
  std::vector<bool> expected_result{false, true};
  EXPECT_EQ(*serialized_val, expected_result);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
