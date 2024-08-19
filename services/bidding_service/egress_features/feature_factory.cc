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

#include "services/bidding_service/egress_features/feature_factory.h"

#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "services/bidding_service/egress_features/boolean_feature.h"
#include "services/bidding_service/egress_features/bucket_feature.h"
#include "services/bidding_service/egress_features/egress_feature.h"
#include "services/bidding_service/egress_features/histogram_feature.h"
#include "services/bidding_service/egress_features/signed_int_feature.h"
#include "services/bidding_service/egress_features/unsigned_int_feature.h"
#include "services/common/util/data_util.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

inline constexpr absl::string_view kBooleanFeatureTag = "boolean-feature";
inline constexpr absl::string_view kSignedIntFeatureTag =
    "signed-integer-feature";
inline constexpr absl::string_view kUnsignedIntFeatureTag =
    "unsigned-integer-feature";
inline constexpr absl::string_view kBucketFeatureTag = "bucket-feature";
inline constexpr absl::string_view kHistogramFeatureTag = "histogram-feature";
inline constexpr int kNumFeatureTypes = 5;
inline constexpr std::array<absl::string_view, kNumFeatureTypes> kFeatureTypes =
    {kBooleanFeatureTag, kSignedIntFeatureTag, kUnsignedIntFeatureTag,
     kBucketFeatureTag, kHistogramFeatureTag};

}  // namespace

absl::StatusOr<std::unique_ptr<EgressFeature>> CreateEgressFeature(
    absl::string_view name, uint32_t size,
    std::shared_ptr<rapidjson::Value> value) {
  const int feat_index = FindItemIndex(kFeatureTypes, name);
  switch (feat_index) {
    case 0:  // kBooleanFeatureTag
      return std::make_unique<BooleanFeature>(size);
    case 1:  // kSignedIntFeatureTag
      return std::make_unique<SignedIntFeature>(size);
    case 2:  // kUnsignedIntFeatureTag
      return std::make_unique<UnsignedIntFeature>(size);
    case 3:  // kBucketFeatureTag
      return std::make_unique<BucketFeature>(size);
    case 4:  // kHistogramFeatureTag
      return std::make_unique<HistogramFeature>(size, std::move(value));
    default:
      std::string err =
          absl::StrCat("Unidentified feature type provided: ", name);
      PS_VLOG(5) << err;
      return absl::InvalidArgumentError(std::move(err));
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
