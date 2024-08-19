// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/utils/egress.h"

#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "third_party/cddl/include/cddl.h"

namespace privacy_sandbox::bidding_auction_servers {

bool AdtechEgresSchemaValid(absl::string_view adtech_schema,
                            absl::string_view cddl_spec) {
  return validate_json_str(cddl_spec.data(), adtech_schema.data());
}

void UnsignedIntToBits(uint num, std::vector<bool>& bit_rep) {
  int i = bit_rep.size() - 1;
  while (num) {
    if (num % 2 > 0) {
      bit_rep[i] = true;
    }
    num /= 2;
    --i;
  }
}

std::string DebugString(const std::vector<bool>& bit_vector) {
  return absl::StrJoin(bit_vector, "", [](std::string* out, const bool val) {
    out->append(val ? "1" : "0");
  });
}

std::string DebugString(const std::vector<uint8_t>& byte_array) {
  return absl::StrJoin(byte_array, "", [](std::string* out, const uint8_t val) {
    std::vector<bool> bit_representation(sizeof(val) * CHAR_BIT, false);
    UnsignedIntToBits(val, bit_representation);
    out->append(DebugString(bit_representation));
  });
}

}  // namespace privacy_sandbox::bidding_auction_servers
