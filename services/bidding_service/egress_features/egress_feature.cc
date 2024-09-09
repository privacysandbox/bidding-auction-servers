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

#include "services/bidding_service/egress_features/egress_feature.h"

#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "rapidjson/document.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

EgressFeature::EgressFeature(uint32_t size) : size_(size) {}
EgressFeature::EgressFeature(uint32_t size,
                             std::shared_ptr<rapidjson::Value> schema_value)
    // NOLINTNEXTLINE
    : size_(size), schema_value_(schema_value) {}
EgressFeature::EgressFeature(const EgressFeature& other) {
  schema_value_ = other.schema_value_;
  size_ = other.size_;
}

uint32_t EgressFeature::Size() const { return size_; }

absl::Status EgressFeature::SetValue(rapidjson::Value value, bool verify_type) {
  DCHECK(!is_value_set_);
  std::string observed_feat_type;
  PS_ASSIGN_IF_PRESENT(observed_feat_type, value, "name", String);
  if (verify_type) {
    absl::string_view expected_feat_type = Type();
    if (observed_feat_type != expected_feat_type) {
      return absl::InvalidArgumentError(
          absl::StrCat("Type of the feature in payload '", observed_feat_type,
                       "' doesn't match with the name in the schema: '",
                       expected_feat_type, "' at the same index"));
    }
  }
  value_ = std::move(value);
  is_value_set_ = true;
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
