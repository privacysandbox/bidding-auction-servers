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

#include "services/bidding_service/egress_features/feature_factory.h"

#include <gmock/gmock-matchers.h>

#include <rapidjson/document.h>

#include "absl/log/check.h"
#include "gtest/gtest.h"
#include "services/bidding_service/egress_features/boolean_feature.h"
#include "services/bidding_service/egress_features/bucket_feature.h"
#include "services/bidding_service/egress_features/histogram_feature.h"
#include "services/bidding_service/egress_features/signed_int_feature.h"
#include "services/bidding_service/egress_features/unsigned_int_feature.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::HasSubstr;

TEST(FeatureFactoryTest, CreatesBooleanFeature) {
  auto feat =
      CreateEgressFeature("boolean-feature", /*size=*/1, /*value=*/nullptr);
  CHECK_OK(feat);
  EXPECT_NE(dynamic_cast<BooleanFeature*>(feat->get()), nullptr);
}

TEST(FeatureFactoryTest, CreatesUnsignedIntFeature) {
  auto feat = CreateEgressFeature("unsigned-integer-feature", /*size=*/2,
                                  /*value=*/nullptr);
  CHECK_OK(feat);
  EXPECT_NE(dynamic_cast<UnsignedIntFeature*>(feat->get()), nullptr);
}

TEST(FeatureFactoryTest, CreatesSignedIntFeature) {
  auto feat = CreateEgressFeature("signed-integer-feature", /*size=*/2,
                                  /*value=*/nullptr);
  CHECK_OK(feat);
  EXPECT_NE(dynamic_cast<SignedIntFeature*>(feat->get()), nullptr);
}

TEST(FeatureFactoryTest, CreatesBucketFeature) {
  auto feat =
      CreateEgressFeature("bucket-feature", /*size=*/3, /*value=*/nullptr);
  CHECK_OK(feat);
  EXPECT_NE(dynamic_cast<BucketFeature*>(feat->get()), nullptr);
}

TEST(FeatureFactoryTest, HistogramFeature) {
  auto rapidjson_value = std::make_shared<rapidjson::Value>();
  auto feat =
      CreateEgressFeature("histogram-feature", /*size=*/4, rapidjson_value);
  CHECK_OK(feat);
  EXPECT_NE(dynamic_cast<HistogramFeature*>(feat->get()), nullptr);
}

TEST(FeatureFactoryTest, ComplainsOnNonExistentFeatureType) {
  auto feat =
      CreateEgressFeature("unknown-feature", /*size=*/5, /*value=*/nullptr);
  ASSERT_FALSE(feat.ok());
  EXPECT_THAT(feat.status().message(),
              HasSubstr("Unidentified feature type provided"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
