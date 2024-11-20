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

#include "services/bidding_service/egress_schema_cache.h"

#include <utility>

#include <include/gmock/gmock-matchers.h>

#include "absl/log/check.h"
#include "gtest/gtest.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::HasSubstr;

class EgressSchemaCacheTest : public ::testing::Test {
 public:
  void SetUp() override {
    server_common::log::SetGlobalPSVLogLevel(10);
    CHECK_OK(cddl_spec_cache_->Init());
    egress_schema_cache_ =
        std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache_));
  }

 protected:
  std::unique_ptr<CddlSpecCache> cddl_spec_cache_ =
      std::make_unique<CddlSpecCache>(
          "services/bidding_service/egress_cddl_spec/");
  std::unique_ptr<EgressSchemaCache> egress_schema_cache_;
};

TEST_F(EgressSchemaCacheTest, FailsOnAbsentVersion) {
  auto egress_features = egress_schema_cache_->Get("fake");
  ASSERT_FALSE(egress_features.ok());
  EXPECT_THAT(egress_features.status().message(), HasSubstr("not found"));
}

TEST_F(EgressSchemaCacheTest, UpdatesCacheSuccessfully) {
  CHECK_OK(egress_schema_cache_->Update(R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "boolean-feature"
        },
        {
          "name": "unsigned-integer-feature",
          "size": 4
        },
        {
          "name": "signed-integer-feature",
          "size": 4
        },
        {
          "name": "bucket-feature",
          "size": 2
        },
        {
          "name": "histogram-feature",
          "size": 3,
          "value": [
            {
              "name": "unsigned-integer-feature",
              "size": 4
            },
            {
              "name": "signed-integer-feature",
              "size": 2
            },
            {
              "name": "unsigned-integer-feature",
              "size": 4
            }
          ]
        }
      ]
    }
  )JSON"));
  auto egress_features = egress_schema_cache_->Get(kDefaultEgressSchemaId);
  CHECK_OK(egress_features);
  EXPECT_EQ(egress_features->features.size(), 5);
  EXPECT_EQ(egress_features->version, 2);

  CHECK_OK(egress_schema_cache_->Update(R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "boolean-feature"
        }
      ]
    }
  )JSON"));
  auto egress_features_2 = egress_schema_cache_->Get(kDefaultEgressSchemaId);
  CHECK_OK(egress_features_2);
  EXPECT_EQ(egress_features_2->features.size(), 1);
  EXPECT_EQ(egress_features_2->version, 2);
}

TEST_F(EgressSchemaCacheTest, FailsOnGetOfNonConformantSchemaVersion) {
  auto updated = egress_schema_cache_->Update(R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "boolean-feature-set"
        }
      ]
    }
  )JSON");
  ASSERT_FALSE(updated.ok());
  EXPECT_THAT(updated.message(),
              HasSubstr("doesn't conform with the CDDL spec"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
