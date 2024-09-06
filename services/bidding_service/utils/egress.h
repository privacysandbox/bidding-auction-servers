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

#ifndef SERVICES_BIDDING_SERVICE_UTILS_EGRESS_H_
#define SERVICES_BIDDING_SERVICE_UTILS_EGRESS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Verifies adtech provided schema conformance with the give CDDL spec.
bool AdtechEgresSchemaValid(absl::string_view adtech_schema,
                            absl::string_view cddl_spec);

// Converts the provided unsigned number to bit representation by setting the
// right bits in the provided vector. Caller is responsible for ensuring that
// the passed in vector is big enough to allow for conversion.
void UnsignedIntToBits(uint num, std::vector<bool>& bit_rep);

// Returns a bit-string corresponding to the passed in bit vector.
std::string DebugString(const std::vector<bool>& bit_vector);

// Returns a bit-string corresponding to the passed in byte array.
std::string DebugString(const std::vector<uint8_t>& byte_array);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_UTILS_EGRESS_H_
