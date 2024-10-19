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

#include "services/bidding_service/egress_features/unsigned_int_feature.h"

#include <optional>
#include <utility>

#include "services/bidding_service/utils/egress.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

UnsignedIntFeature::UnsignedIntFeature(uint32_t size) : EgressFeature(size) {}

UnsignedIntFeature::UnsignedIntFeature(const UnsignedIntFeature& other)
    : EgressFeature(other) {}

absl::StatusOr<std::unique_ptr<EgressFeature>> UnsignedIntFeature::Copy()
    const {
  return std::make_unique<UnsignedIntFeature>(*this);
}

absl::string_view UnsignedIntFeature::Type() const {
  return "unsigned-integer-feature";
}

absl::StatusOr<std::vector<bool>> UnsignedIntFeature::Serialize() {
  if (!is_value_set_) {
    PS_VLOG(5)
        << "Unsigned int feature value has not been set, returning default";
    return std::vector<bool>(size_, false);
  }
  std::optional<uint> value;
  PS_ASSIGN_IF_PRESENT(value, value_, "value", Uint);
  if (!value) {
    return absl::InvalidArgumentError(
        "Int feature not found in the egress payload");
  }
  // Signed integer range example:
  //  For 3-bits signed int, the max positive number will be: 111 = 7
  if (*value > pow(2, size_) - 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Out of bound error: Uint feature value: ", *value,
        " can not be represented in ", size_, " bits allowed by the schema"));
  }
  std::vector<bool> bit_rep(size_, false);
  UnsignedIntToBits(*value, bit_rep);
  return bit_rep;
}

}  // namespace privacy_sandbox::bidding_auction_servers
