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

namespace privacy_sandbox::bidding_auction_servers {

// This method expects a plaintext json with the following fields.
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
// It returns a SelectAdRequest for testing B&A servers in "test_mode"
// by performing the following operations with the input fields -
// 1. Create a ProtectedAudienceInput object.
// 2. Encode and compress the Buyer Input Map, and copy over to the
// ProtectedAudienceInput object from 1.
// 3. Copy over the other fields from raw_protected_audience_input into the
// ProtectedAudienceInput object from 1.
// 4. Encode the ProtectedAudienceInput object from 1.
// 5. Encrypt the ProtectedAudienceInput object from 1 using the B&A "test_mode"
// hardcoded keys.
// 6. Copy the encrypted ProtectedAudienceInput object and auction_config from
// the input JSON to a SelectAdRequest object.
// 7. Convert the SelectAdRequest object to a JSON string.
// The protected_audience_ciphertext is encoded, encrypted and compressed in the
// same format as expected in the ciphertext from a CLIENT_TYPE_BROWSER.
// This will extended in the future to support CLIENT_TYPE_ANDROID as well.
std::pair<std::unique_ptr<SelectAdRequest>,
          quiche::ObliviousHttpRequest::Context>
PackagePlainTextSelectAdRequest(
    absl::string_view input_json_str, ClientType client_type,
    const HpkeKeyset& keyset, bool enable_debug_reporting = false,
    absl::string_view protected_app_signals_json = "");

// This method returns a SelectAdRequest json for testing B&A servers in
// "test_mode" using the PackagePlainTextSelectAdRequest method.
std::string PackagePlainTextSelectAdRequestToJson(
    absl::string_view input_json_str, ClientType client_type,
    const HpkeKeyset& keyset, bool enable_debug_reporting = false);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // TOOLS_PAYLOAD_GENERATOR_PAYLOAD_PACKAGING_H_
