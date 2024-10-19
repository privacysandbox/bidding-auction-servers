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

#include "services/bidding_service/egress_features/boolean_feature.h"

#include <utility>

#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

BooleanFeature::BooleanFeature(uint32_t size) : EgressFeature(size) {}
BooleanFeature::BooleanFeature(const BooleanFeature& other)
    : EgressFeature(other) {}

absl::string_view BooleanFeature::Type() const { return "boolean-feature"; }

absl::StatusOr<std::vector<bool>> BooleanFeature::Serialize() {
  if (!is_value_set_) {
    PS_VLOG(5) << "Boolean Feature value has not been set, returning a default";
    return std::vector<bool>(1, false);
  }
  std::optional<bool> value;
  PS_ASSIGN_IF_PRESENT(value, value_, "value", Bool);
  if (!value) {
    return absl::InvalidArgumentError(
        "Value in the egress payload didn't have a bool");
  }
  return std::vector<bool>{*value};
}

absl::StatusOr<std::unique_ptr<EgressFeature>> BooleanFeature::Copy() const {
  return std::make_unique<BooleanFeature>(*this);
}

}  // namespace privacy_sandbox::bidding_auction_servers
