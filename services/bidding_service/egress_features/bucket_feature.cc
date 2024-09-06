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

#include "absl/strings/str_join.h"
#include "services/bidding_service/egress_features/boolean_feature.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

BucketFeature::BucketFeature(uint32_t size) : EgressFeature(size) {}

BucketFeature::BucketFeature(const BucketFeature& other)
    : EgressFeature(other) {}

absl::StatusOr<std::unique_ptr<EgressFeature>> BucketFeature::Copy() const {
  return std::make_unique<BucketFeature>(*this);
}

absl::string_view BucketFeature::Type() const { return "bucket-feature"; }

absl::StatusOr<std::vector<bool>> BucketFeature::Serialize() {
  if (!is_value_set_) {
    PS_VLOG(5) << "Bucket Feature value has not been set, returning default";
    return std::vector<bool>(size_, false);
  }
  PS_ASSIGN_OR_RETURN(auto buckets, GetArrayMember(value_, "value"));
  if (buckets.Size() != size_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Number of buckets in feature payload (", buckets.Size(),
                     ") doesn't match schema (", size_, ")"));
  }
  std::vector<bool> serialized_feat;
  serialized_feat.reserve(buckets.Size());
  int idx = size_ - 1;
  for (rapidjson::Value& feat : buckets) {
    if (!feat.IsObject()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Feature at index ", size_ - (idx + 1),
                       " is not an object, expected object"));
    }
    BooleanFeature bool_feat(/*size=*/1);
    PS_RETURN_IF_ERROR(bool_feat.SetValue(std::move(feat),
                                          /*verify_type=*/false));
    PS_ASSIGN_OR_RETURN(auto serialized_bool, bool_feat.Serialize());
    DCHECK_EQ(serialized_bool.size(), 1)
        << "Unexpected boolean bit vector size";
    serialized_feat.emplace_back(serialized_bool[0]);
    --idx;
  }
  std::reverse(serialized_feat.begin(), serialized_feat.end());
  return serialized_feat;
}

}  // namespace privacy_sandbox::bidding_auction_servers
