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
#include "services/seller_frontend_service/util/framing_utils.h"

#include <algorithm>

#include "absl/numeric/bits.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

// 1 byte for version + compression details.
constexpr int kVersionCompressionSize = 1;

// 4-bytes specifying the size of the actual payload.
constexpr int kPayloadLength = 4;

// Gets size of the complete payload including the preamble expected by
// android, which is: 1 byte (containing version, compression details), 4 bytes
// indicating the length of the actual encoded response and any other padding
// required to make the complete payload a power of 2.
size_t GetEncodedDataSize(size_t encapsulated_payload_size) {
  size_t total_payload_size =
      kVersionCompressionSize + kPayloadLength + encapsulated_payload_size;
  // Ensure that the payload size is a power of 2.
  return std::max(absl::bit_ceil(total_payload_size), kMinAuctionResultBytes);
}

}  // namespace privacy_sandbox::bidding_auction_servers
