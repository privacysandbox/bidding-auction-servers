// Copyright 2023 Google LLC
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

#include "services/seller_frontend_service/test/app_test_utils.h"

#include <utility>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "services/common/compression/gzip.h"

namespace privacy_sandbox::bidding_auction_servers {

using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = ::google::protobuf::Map<std::string, BuyerInput>;
using DecodedBuyerInputForBiddings =
    ::google::protobuf::Map<std::string, BuyerInputForBidding>;

EncodedBuyerInputs GetProtoEncodedBuyerInputs(
    const DecodedBuyerInputs& buyer_inputs) {
  EncodedBuyerInputs encoded_buyer_inputs;
  for (const auto& [buyer, buyer_input] : buyer_inputs) {
    absl::StatusOr<std::string> compressed_buyer_input =
        GzipCompress(buyer_input.SerializeAsString());
    DCHECK(compressed_buyer_input.ok()) << compressed_buyer_input.status();
    encoded_buyer_inputs.emplace(buyer, std::move(*compressed_buyer_input));
  }
  return encoded_buyer_inputs;
}

// Encodes and compresses the passed in buyer inputs.
EncodedBuyerInputs GetProtoEncodedBuyerInputs(
    const DecodedBuyerInputForBiddings& buyer_inputs) {
  EncodedBuyerInputs encoded_buyer_inputs;
  for (const auto& [buyer, buyer_input] : buyer_inputs) {
    absl::StatusOr<std::string> compressed_buyer_input =
        GzipCompress(buyer_input.SerializeAsString());
    DCHECK(compressed_buyer_input.ok()) << compressed_buyer_input.status();
    encoded_buyer_inputs.emplace(buyer, std::move(*compressed_buyer_input));
  }
  return encoded_buyer_inputs;
}

}  // namespace privacy_sandbox::bidding_auction_servers
