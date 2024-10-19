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

#include "absl/strings/str_join.h"
#include "services/bidding_service/egress_features/signed_int_feature.h"
#include "services/bidding_service/egress_features/unsigned_int_feature.h"
#include "services/bidding_service/utils/egress.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

HistogramFeature::HistogramFeature(
    uint32_t size, std::shared_ptr<rapidjson::Value> schema_value)
    // NOLINTNEXTLINE
    : EgressFeature(size, std::move(schema_value)) {}

HistogramFeature::HistogramFeature(const HistogramFeature& other)
    : EgressFeature(other) {}

absl::StatusOr<std::unique_ptr<EgressFeature>> HistogramFeature::Copy() const {
  return std::make_unique<HistogramFeature>(*this);
}

uint32_t HistogramFeature::Size() const {
  if (!histogram_size_) {
    uint32_t total_size = 0;
    if (auto histogram_schema = GetArrayMember(*schema_value_, "value");
        histogram_schema.ok()) {
      uint32_t feat_size = 0;
      for (const auto& feat_schema : *histogram_schema) {
        PS_ASSIGN_IF_PRESENT(feat_size, feat_schema, "size", Uint);
        total_size += feat_size;
      }
      histogram_size_ = total_size;
    } else {
      PS_VLOG(5) << "Failed to get size of histogram (bad schema?)";
      histogram_size_ = 0;
    }
  }
  return *histogram_size_;
}

absl::string_view HistogramFeature::Type() const { return "histogram-feature"; }

absl::StatusOr<std::vector<bool>> HistogramFeature::Serialize() {
  if (!is_value_set_) {
    PS_VLOG(5) << "Histogram Feature value has not been set, returning default";
    return std::vector<bool>(Size(), false);
  }
  PS_ASSIGN_OR_RETURN(auto histogram_val, GetArrayMember(value_, "value"));
  PS_ASSIGN_OR_RETURN(auto histogram_schema,
                      GetArrayMember(*schema_value_, "value"));
  if (histogram_val.Size() != histogram_schema.Size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Number of buckets in histogram feature payload (",
                     histogram_val.Size(), ") doesn't match schema (",
                     histogram_schema.Size(), ")"));
  }
  std::vector<std::vector<bool>> serialized_feat;
  serialized_feat.reserve(size_);
  int idx = size_ - 1;
  uint32_t total_size = 0;
  for (rapidjson::Value& feat : histogram_val) {
    if (!feat.IsObject()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Feature at index ", size_ - (idx + 1),
                       " is not an object, expected object"));
    }
    const rapidjson::Value& feat_schema = histogram_schema[size_ - (idx + 1)];
    std::string feat_name;
    PS_ASSIGN_IF_PRESENT(feat_name, feat_schema, "name", String);
    DCHECK(!feat_name.empty())
        << "No name found in schema for feature at idx: " << size_ - (idx + 1);
    uint32_t feat_size = 0;
    PS_ASSIGN_IF_PRESENT(feat_size, feat_schema, "size", Uint);
    if (feat_name == "unsigned-integer-feature") {
      UnsignedIntFeature unsigned_int_feat(feat_size);
      total_size += feat_size;
      PS_RETURN_IF_ERROR(unsigned_int_feat.SetValue(std::move(feat)));
      PS_ASSIGN_OR_RETURN(auto serialized_num, unsigned_int_feat.Serialize());
      PS_VLOG(5) << "Serialized unsigned int at idx: " << idx
                 << " in histogram is: " << DebugString(serialized_num);
      serialized_feat.emplace_back(std::move(serialized_num));
    } else if (feat_name == "signed-integer-feature") {
      total_size += feat_size;
      SignedIntFeature signed_int_feat(feat_size);
      PS_RETURN_IF_ERROR(signed_int_feat.SetValue(std::move(feat)));
      PS_ASSIGN_OR_RETURN(auto serialized_num, signed_int_feat.Serialize());
      PS_VLOG(5) << "Serialized signed int at idx: " << idx
                 << " in histogram is: " << DebugString(serialized_num);
      serialized_feat.emplace_back(std::move(serialized_num));
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("Only signed and unsigned integer feature types are "
                       "supported for histogram, got: ",
                       feat_name));
    }
    --idx;
  }
  std::vector<bool> flattened_vector;
  flattened_vector.reserve(total_size);
  for (auto feat_it = serialized_feat.rbegin();
       feat_it != serialized_feat.rend(); ++feat_it) {
    for (const bool feat_val : *feat_it) {
      flattened_vector.emplace_back(feat_val);
    }
  }
  return flattened_vector;
}

}  // namespace privacy_sandbox::bidding_auction_servers
