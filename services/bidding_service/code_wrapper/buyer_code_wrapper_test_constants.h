
//  Copyright 2023 Google LLC
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
#ifndef FLEDGE_SERVICES_BUYER_CODE_WRAPPER_TEST_CONSTANTS_H_
#define FLEDGE_SERVICES_BUYER_CODE_WRAPPER_TEST_CONSTANTS_H_

#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kBuyerBaseCode_template = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }
    function generateBid(interest_group, auction_signals,buyer_signals,
                          trusted_bidding_signals,
                          device_signals){
    const bid = fibonacci(Math.floor(Math.random() * 30 + 1));
    console.log("Logging from generateBid");
    return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
)JS_CODE";
constexpr absl::string_view kExpectedGenerateBidCode_template = R"JS_CODE(
  const globalWasmHex = [];
  const globalWasmHelper = globalWasmHex.length ? new WebAssembly.Module(Uint8Array.from(globalWasmHex)) : null;

    function generateBidEntryFunction(interest_group,
                                auction_signals,
                                buyer_signals,
                                trusted_bidding_signals,
                                device_signals,
                                enable_logging){

    device_signals.wasmHelper = globalWasmHelper;

    var ps_logs = [];
    if(enable_logging){
      console.log = function(...args) {
        ps_logs.push(JSON.stringify(args))
      }
    }
    generateBidResponse = generateBid(interest_group,
                              auction_signals,
                              buyer_signals,
                              trusted_bidding_signals,
                              device_signals)
    return {
      response: generateBidResponse,
      logs: ps_logs
    }
 }

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }
    function generateBid(interest_group, auction_signals,buyer_signals,
                          trusted_bidding_signals,
                          device_signals){
    const bid = fibonacci(Math.floor(Math.random() * 30 + 1));
    console.log("Logging from generateBid");
    return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
)JS_CODE";

}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // FLEDGE_SERVICES_BUYER_CODE_WRAPPER_TEST_CONSTANTS_H_
