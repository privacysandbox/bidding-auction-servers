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

#ifndef TOOLS_PAYLOAD_GENERATOR_PAYLOAD_PACKAGING_H_
#define TOOLS_PAYLOAD_GENERATOR_PAYLOAD_PACKAGING_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "quiche/oblivious_http/oblivious_http_client.h"
#include "services/common/test/utils/ohttp_utils.h"

const char kAuctionConfigField[] = "auction_config";
const char kProtectedAuctionInputField[] = "raw_protected_audience_input";
const char kOldBuyerInputMapField[] = "buyer_input";
const char kBuyerInputMapField[] = "raw_buyer_input";
const char kComponentAuctionsField[] = "raw_component_auction_results";

namespace privacy_sandbox::bidding_auction_servers {

// This method expects a plaintext json. For single seller/component
// auctions, it expects the following fields -
// {
//    "auction_config" : {
//          "seller_signals": "...",
//          "per_buyer_config": {"buyer1" : {
//              "buyer_signals": "...",
//              ....
//          }}
//          .....
//    }
//    "raw_protected_audience_input": {
//        "raw_buyer_input" : {"buyer1": {
//            "interest_groups": [{
//                "name": "IG1",
//                "bidding_signals_keys":["IG1",..],
//                .....
//            }]
//        }},
//        "publisher_name": "testPublisher.com",
//        .....
//    }
// }
// For this input, it returns a SelectAdRequest for testing B&A servers
// by performing the following operations with the input fields -
// 1. Create a ProtectedAuctionInput object.
// 2. Encode and compress the Buyer Input Map, and copy over to the
// ProtectedAuctionInput object from 1.
// 3. Copy over the other fields from raw_protected_audience_input into the
// ProtectedAuctionInput object from 1.
// 4. Encode the ProtectedAuctionInput object from 1.
// 5. Encrypt the ProtectedAuctionInput object from 1 using the B&A "test_mode"
// hardcoded keys.
// 6. Copy the encrypted ProtectedAuctionInput object and auction_config from
// the input JSON to a SelectAdRequest object.
// 7. Convert the SelectAdRequest object to a JSON string.
// The protected_audience_ciphertext is encoded, encrypted and compressed in the
// same format as expected in the ciphertext from a CLIENT_TYPE_BROWSER.
// This will extended in the future to support CLIENT_TYPE_ANDROID as well.

// For top level auctions, this method expects a plaintext json
// with the following fields -
// {
//    "auction_config" : {
//          "seller_signals": "...",
//          "per_buyer_config": {"buyer1" : {
//              "buyer_signals": "...",
//              ....
//          }}
//          .....
//    }
//    "raw_protected_audience_input": {
//     "publisher_name": "testPublisher.com",
//     .....
// },
// "raw_component_auction_results": [{
//         "ad_render_url": "URL",
//         "interest_group_origin": "testIG.com",
//         "interest_group_name": "testIG",
//         "interest_group_owner": "testIG.com",
//         "bidding_groups": {},
//         "score": 4.9,
//         "bid": 0.2,
//         "bid_currency": "USD",
//         "ad_metadata": "{}",
//         "top_level_seller": "SFE-domain.com",
//         "auction_params": {
//             "component_seller": "test_seller.com"
//         }
//     },
//     .....
// ]
// }
// For this input, it returns a SelectAdRequest for testing B&A servers
// by performing the following operations with the input fields -
// 1. Create a ProtectedAuctionInput object with empty Buyer Input Map.
// 2. Copy over the other fields from raw_protected_audience_input into the
// ProtectedAuctionInput object from 1.
// 3. Encode the ProtectedAuctionInput object from 1.
// 4. Encrypt the ProtectedAuctionInput object from 1 using the provided keys.
// 5. Copy the encrypted ProtectedAuctionInput object and auction_config from
// the input JSON to a SelectAdRequest object.
// 6. Create ComponentAuctionResults from the raw_component_auction_results
// field.
// 7. Encode, compress and HPKE encrypt the ComponentAuctionResults in step 6.
// 8. Convert the SelectAdRequest object to a JSON string.
// The protected_audience_ciphertext is encoded, encrypted and compressed in the
// same format as expected in the ciphertext from a Server Component Auction.
std::pair<std::unique_ptr<SelectAdRequest>,
          quiche::ObliviousHttpRequest::Context>
PackagePlainTextSelectAdRequest(
    absl::string_view input_json_str, ClientType client_type,
    const HpkeKeyset& keyset, bool enable_debug_reporting = false,
    std::optional<bool> enable_debug_info = std::nullopt,
    absl::string_view protected_app_signals_json = "",
    std::optional<bool> enable_unlimited_egress = std::nullopt);

// This method returns a SelectAdRequest json for testing B&A servers in
// "test_mode" using the PackagePlainTextSelectAdRequest method.
std::string PackagePlainTextSelectAdRequestToJson(
    absl::string_view input_json_str, ClientType client_type,
    const HpkeKeyset& keyset, bool enable_debug_reporting = false,
    std::optional<bool> enable_debug_info = std::nullopt,
    std::optional<bool> enable_unlimited_egress = std::nullopt);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // TOOLS_PAYLOAD_GENERATOR_PAYLOAD_PACKAGING_H_
