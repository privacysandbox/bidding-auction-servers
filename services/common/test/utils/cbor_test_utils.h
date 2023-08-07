/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_TEST_CBOR_UTILS_H_
#define SERVICES_COMMON_TEST_CBOR_UTILS_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.grpc.pb.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {

using PrevWin = std::pair<uint64_t, std::string>;

// Decodes the CBOR-serialized AuctionResult to proto.
absl::StatusOr<AuctionResult> CborDecodeAuctionResultToProto(
    const std::string& serialized_input);

// Converts the raw protected audience input into CBOR encoded byte string.
absl::StatusOr<std::string> CborEncodeProtectedAudienceProto(
    const ProtectedAudienceInput& protected_audience_input);

// Encodes and compresses each BuyerInput in the input map. Returns the encoded
// BuyerInput Map (note that keys in the map are not encoded).
absl::StatusOr<google::protobuf::Map<std::string, std::string>>
GetEncodedBuyerInputMap(
    const google::protobuf::Map<std::string, BuyerInput>& buyer_inputs);

// Converts the passed in CBOR data handle to the serialized CBOR byte-string.
std::string SerializeCbor(cbor_item_t* root);

// Decodes cbor string input to std::string
inline std::string CborDecodeString(cbor_item_t* input) {
  return std::string(reinterpret_cast<char*>(cbor_string_handle(input)),
                     cbor_string_length(input));
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_TEST_CBOR_UTILS_H_
