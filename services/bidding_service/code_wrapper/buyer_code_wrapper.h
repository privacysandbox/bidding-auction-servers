
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
#ifndef SERVICES_AUCTION_SERVICE_BUYER_CODE_WRAPPER_H_
#define SERVICES_AUCTION_SERVICE_BUYER_CODE_WRAPPER_H_
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "services/common/clients/config/trusted_server_config_client.h"

namespace privacy_sandbox::bidding_auction_servers {

// Returns the complete wrapped code for Buyer.
// The function adds wrappers to the Buyer provided generateBid function.
// This enables:
// - Generation of event level debug reporting
// - Exporting console.logs from the AdTech execution.
// - wasmHelper added to device_signals
std::string GetBuyerWrappedCode(absl::string_view adtech_js,
                                absl::string_view adtech_wasm);

// Wrapper Javascript over AdTech code.
// This wrapper supports the features below:
//- Exporting logs to Bidding Service using console.log
//- Hooks in wasm module
inline constexpr absl::string_view kEntryFunction = R"JS_CODE(
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
)JS_CODE";

// This is used to create a javascript array that contains a hex representation
// of the raw wasm bytecode.
inline constexpr absl::string_view kWasmModuleTemplate = R"JS_CODE(
  const globalWasmHex = [%s];
  const globalWasmHelper = globalWasmHex.length ? new WebAssembly.Module(Uint8Array.from(globalWasmHex)) : null;
)JS_CODE";

}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_AUCTION_SERVICE_BUYER_CODE_WRAPPER_H_
