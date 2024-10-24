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

#include "services/bidding_service/egress_features/signed_int_feature.h"

#include <optional>
#include <utility>

#include "services/bidding_service/utils/egress.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers {

SignedIntFeature::SignedIntFeature(uint32_t size) : EgressFeature(size) {}

SignedIntFeature::SignedIntFeature(const SignedIntFeature& other)
    : EgressFeature(other) {}

absl::string_view SignedIntFeature::Type() const {
  return "signed-integer-feature";
}

absl::StatusOr<std::unique_ptr<EgressFeature>> SignedIntFeature::Copy() const {
  return std::make_unique<SignedIntFeature>(*this);
}

absl::StatusOr<std::vector<bool>> SignedIntFeature::Serialize() {
  if (!is_value_set_) {
    PS_VLOG(5) << "Signed int feature value has not been set, default";
    return std::vector<bool>(size_, false);
  }
  std::optional<int> value;
  PS_ASSIGN_IF_PRESENT(value, value_, "value", Int);
  if (!value) {
    return absl::InvalidArgumentError(
        "Int feature not found in the egress payload");
  }
  // Signed integer range example:
  //  For 3-bits signed int, the max positive number will be: 011 = 3
  //  and the max negative int will be 100 = -4
  if (*value >= pow(2, size_ - 1) || *value < -pow(2, size_ - 1)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Out of bound error: Int feature value: ", *value,
        " can not be represented in ", size_, " bits allowed by the schema"));
  }
  std::vector<bool> bit_rep(size_, false);
  if (*value < 0) {
    *value += pow(2, size_ - 1);
    UnsignedIntToBits(*value, bit_rep);
    bit_rep[0] = true;  // Set signed bit
  } else {
    UnsignedIntToBits(*value, bit_rep);
  }
  return bit_rep;
}

}  // namespace privacy_sandbox::bidding_auction_servers
