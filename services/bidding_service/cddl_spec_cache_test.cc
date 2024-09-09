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

#include "services/bidding_service/cddl_spec_cache.h"

#include <gmock/gmock-matchers.h>

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "services/common/util/file_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::HasSubstr;

inline constexpr absl::string_view kSpec100Contents =
    R"""(; An ad-tech controlled `schema` of `feature`s to egress. Ad-techs provide
; their `schema` as a JSON file alongside their `generateBid()` implementation,
; and it is used during validation to ensure that `payload`s are formatted as
; expected.
schema = {
  features: [* feature_types],
  ; Version of the CDDL used when constructing this `schema` instance.
  cddl_version: cddl_version,
  ; Version of this schema instance itself. Will be prepended to the wire format
  ; of the `payload` after validation. 3 bits, so 8 maximum concurrent `schema`s
  ; are supported.
  version: uint .le 7
}

; A platform-defined enumeration of all `feature` values we support. During
; validation, we will enforce that all `feature`s in a `payload` are in this
; enumeration. We will also analyze `feature` instances to determine the number
; of bits of information they contain so that we can be sure the sum of the
; information does not overflow the system-controlled maximum.
;
; Each concrete `feature` will have a `type` field; see `feature_types`.
feature /= boolean-feature / signed-integer-feature /
           unsigned-integer-feature / bucket-feature  /
           histogram-feature  ; Additional `feature` types here.

; All supported `feature_types`. `feature`s hold values, and each one has a
; matching `_type`, used in `schema` to define payload structure.
;
; All `_type`s must have a `name`, and may optionally have other fields.
feature_types /= boolean-feature-type / signed-integer-feature-type /
                 unsigned-integer-feature-type / bucket-feature-type  /
                 histogram-feature-type

boolean-feature-type = {
  name: text .regexp "^boolean-feature$"
}

boolean-feature = {
  name: text .regexp "^boolean-feature$",
  value: bool,
}

unsigned-integer-feature-type = {
  name: text .regexp "^unsigned-integer-feature$"
  size: uint,
}

unsigned-integer-feature = {
  name: text .regexp "^unsigned-integer-feature$",
  value: uint,
}

signed-integer-feature-type = {
  name: text .regexp "^signed-integer-feature$"
  size: uint,
}

signed-integer-feature = {
  name: text .regexp "^signed-integer-feature$",
  value: int,
}

bucket-feature-type = {
  name: text .regexp "^bucket-feature$",
  size: uint,  ; number of buckets.
}

bucket-feature = {
  name: text .regexp "^bucket-feature$",
  value: [* bool],
}

histogram-feature-type = {
  name: text .regexp "^histogram-feature$"
  size: uint,
  value: [1* histogram-feature-subtype],
}

histogram-feature-subtype /= unsigned-integer-feature-type / signed-integer-feature-type

histogram-feature = {
  name: text .regexp "^histogram-feature$",
  value: [1* unsigned-integer-feature / signed-integer-feature ],
}

; Version of this CDDL used when validating `schema`s and `payload`s.
cddl_version /= "1.0.0"

; An ad-tech controlled ordered list of `feature` values to egress.
;
; We will cap the amount of information contained in a `payload` instance to a
; system-controlled value (for example, 20 bits). This is not expressed in the
; schema because enforcement is applied to the amount of information in the
; resulting payload on the wire, modulo bookkeeping information.
payload = {
  features: [* feature],  ; Any number of features, subject to size limits.
  ; No version here -- we will attach the `schema` version used during
  ; validation so the ad-tech knows which schema to use during deserialization.
}
)""";

class CddlSpecCacheTest : public ::testing::Test {
 public:
  void SetUp() { CHECK_OK(cddl_spec_cache_.Init()); }
  CddlSpecCache cddl_spec_cache_{"services/bidding_service/egress_cddl_spec/"};
};

TEST_F(CddlSpecCacheTest, LoadsTheSpecs) {
  auto data = cddl_spec_cache_.Get("1.0.0");
  ASSERT_TRUE(data.ok()) << data.status();
  EXPECT_EQ(*data, kSpec100Contents);
}

TEST_F(CddlSpecCacheTest, ComplainsAboutMissingSpecs) {
  auto data = cddl_spec_cache_.Get("0.0.1");
  ASSERT_FALSE(data.ok());
  EXPECT_THAT(data.status().message(),
              HasSubstr("Unable to find the CDDL spec version in cache"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
