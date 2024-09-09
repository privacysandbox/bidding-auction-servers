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

#include "services/bidding_service/utils/egress.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "services/common/util/file_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

inline constexpr absl::string_view kCddlSpec100Path =
    "services/bidding_service/egress_cddl_spec/1.0.0";
inline constexpr absl::string_view kTestCddlSpec = R"""(
schema = {
  features: [* feature_types],
  ; Version of the CDDL used when constructing this `schema` instance.
  cddl_version: cddl_version,
  ; Version of this schema instance itself. Will be prepended to the wire format
  ; of the `payload` after validation. 3 bits, so 8 maximum concurrent `schema`s
  ; are supported. See wire format for details.
  version: uint .le 7
}

feature = boolean-feature / bucket-feature  ; Additional `feature` types here.

feature_types /= boolean-feature-type / bucket-feature-type  ; More here too.

; Type for `boolean-feature`.
boolean-feature-type = {
  name: text .regexp "^boolean-feature$"
}

; A `feature` representing a single boolean value.
;
; 1 bit of information.
boolean-feature = {
  name: text .regexp "^boolean-feature$"
  value: bool,
}

; Type for `bucket-feature`.
bucket-feature-type = {
  name: text .regexp "^bucket-feature$",
  size: uint,  ; number of buckets.
}

; A `feature` representing a bucketized value.
bucket-feature = {
  name: text .regexp "^bucket-feature$",
  value: [* bool],
}

; Version of this CDDL used when validating `schema`s and `payload`s.
cddl_version /= "1.0.0"  ; In v1.0.1, this would be "1.0.0" / "1.0.1".

)""";

TEST(ValidateAdtechSchema, SucceedsWithConformingSchema) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 7,
      "features": [
        {
          "name": "boolean-feature"
        },
        {
          "name": "bucket-feature",
          "size": 2
        }
      ]
    }
  )JSON";
  EXPECT_TRUE(AdtechEgresSchemaValid(adtech_schema, kTestCddlSpec))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is not conformant with spec:\n"
      << kTestCddlSpec;
}

TEST(ValidateAdtechSchema, SucceedsWithOptionalFeatureMissing) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 7,
      "features": [
        {
          "name": "bucket-feature",
          "size": 2
        }
      ]
    }
  )JSON";
  EXPECT_TRUE(AdtechEgresSchemaValid(adtech_schema, kTestCddlSpec))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is not conformant with spec:\n"
      << kTestCddlSpec;
}

TEST(ValidateAdtechSchema, FailsWithMismatchingFeatureName) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 7,
      "features": [
        {
          "name": "unknown-feature",
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, kTestCddlSpec))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is unexpectedly conformant with spec:\n"
      << kTestCddlSpec;
}

TEST(ValidateAdtechSchema, FailsWithUnsupportedCddlVersion) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "10.0.1",
      "version": 7,
      "features": [
        {
          "name": "bucket-feature",
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, kTestCddlSpec))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is unexpectedly conformant with spec:\n"
      << kTestCddlSpec;
}

TEST(ValidateAdtechSchema, FailsWithUnsupportedSchemaVersion) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 9,
      "features": [
        {
          "name": "bucket-feature",
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, kTestCddlSpec))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is unexpectedly conformant with spec:\n"
      << kTestCddlSpec;
}

class CddlSpecTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto file_content = GetFileContent(kCddlSpec100Path, /*log_on_error=*/true);
    CHECK_OK(file_content);
    cddl_spec_ = *std::move(file_content);
  }

  const std::string& CddlSpec() { return cddl_spec_; }

 private:
  std::string cddl_spec_;
};

TEST_F(CddlSpecTest, SucceedsWithValidAdtechSchema) {
  std::string adtech_schema = R"JSON(
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
  )JSON";
  EXPECT_TRUE(AdtechEgresSchemaValid(adtech_schema, CddlSpec()))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is not conformant with spec:\n"
      << CddlSpec();
}

TEST_F(CddlSpecTest, FailsIfSizeNotSpecifiedForUnsignedInteger) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "unsigned-integer-feature"
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, CddlSpec()))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is not unexpectedly conformant with spec:\n"
      << CddlSpec();
}

TEST_F(CddlSpecTest, FailsIfSizeNotSpecifiedForSignedInteger) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "signed-integer-feature"
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, CddlSpec()))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is not unexpectedly conformant with spec:\n"
      << CddlSpec();
}

TEST_F(CddlSpecTest, FailsIfBucketsNotSpecifiedForBucketFeature) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "bucket-feature"
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, CddlSpec()))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is unexpectedly conformant with spec:\n"
      << CddlSpec();
}

TEST_F(CddlSpecTest, FailsIfSizeNotSpecifiedForHistogramFeature) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.0",
      "version": 2,
      "features": [
        {
          "name": "histogram-feature"
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, CddlSpec()))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is unexpectedly conformant with spec:\n"
      << CddlSpec();
}

TEST_F(CddlSpecTest, FailsIfCddlVersionMismatches) {
  std::string adtech_schema = R"JSON(
    {
      "cddl_version": "1.0.1",
      "version": 2,
      "features": [
        {
          "name": "histogram-feature",
          "size": 2
        }
      ]
    }
  )JSON";
  EXPECT_FALSE(AdtechEgresSchemaValid(adtech_schema, CddlSpec()))
      << "Adtech provided schema:\n"
      << adtech_schema << "\n is unexpectedly conformant with spec:\n"
      << CddlSpec();
}

TEST(UnsignedNumberSerialization, CorrectlySerialiesUnsignedInt) {
  std::vector<bool> bit_representation1(3, 0);
  UnsignedIntToBits(7, bit_representation1);
  EXPECT_EQ(bit_representation1, std::vector<bool>({true, true, true}));

  std::vector<bool> bit_representation2(4, 0);
  UnsignedIntToBits(8, bit_representation2);
  EXPECT_EQ(bit_representation2,
            std::vector<bool>({true, false, false, false}));
}

TEST(DebugStringTest, PrintsOutCorrectValueForBoolVector) {
  std::vector<bool> bit_representation{true, false};
  EXPECT_EQ(DebugString(bit_representation), "10");
}

TEST(DebugStringTest, PrintsOutCorrectValueForByteArray) {
  std::vector<uint8_t> byte_array{8, 3};
  EXPECT_EQ(DebugString(byte_array), "0000100000000011");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
