
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
#include "services/bidding_service/code_wrapper/generated_private_aggregation_wrapper.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
struct BuyerCodeWrapperConfig {
  std::string ad_tech_wasm;
  AuctionType auction_type = AuctionType::kProtectedAudience;
  absl::string_view auction_specific_setup =
      "device_signals.wasmHelper = globalWasmHelper;";
  bool enable_private_aggregate_reporting = false;
};

// Returns the complete wrapped code for Buyer.
// The function adds wrappers to the Buyer provided generateBid function.
// This enables:
// - Generation of event level debug reporting
// - Exporting console.logs from the AdTech execution.
// - wasmHelper added to device_signals
std::string GetBuyerWrappedCode(
    absl::string_view ad_tech_js,
    const BuyerCodeWrapperConfig& buyer_code_wrapper_config);

// Returns the complete wrapped code for Buyer.
// The function adds wrappers to the Buyer provided generateBid function.
// This enables:
// - Exporting console.logs from the AdTech execution.
// - wasmHelper added to device_signals
std::string GetProtectedAppSignalsGenericBuyerWrappedCode(
    absl::string_view ad_tech_js, absl::string_view ad_tech_wasm,
    absl::string_view function_name, absl::string_view args);

// Wrapper Javascript over AdTech code.
// This wrapper supports the features below:
//- Exporting logs to Bidding Service using console.log
//- Hooks in wasm module
inline constexpr absl::string_view kEntryFunction = R"JS_CODE(
  function checkMultiBidLimit(generate_bid_response, multiBidLimit, featureFlags) {
    // Drop all the bids if generateBid response exceed multiBidLimit.
    if (generate_bid_response.length > multiBidLimit) {
      generate_bid_response = [];
      if (featureFlags.enable_logging) {
          console.error("[Error: more bids returned by generateBid than multiBidLimit]");
      }
    }
    return generate_bid_response;
  }
    var ps_response = {
          response: [],
          logs: [],
          errors: [],
          warnings: []
        };
    async function generateBidEntryFunction($0, featureFlags){
      if(featureFlags.enable_logging){
        console.log = function(...args) {
          ps_response.logs.push(JSON.stringify(args))
        }
        console.error = function(...args) {
          ps_response.errors.push(JSON.stringify(args))
        }
        console.warn = function(...args) {
          ps_response.warnings.push(JSON.stringify(args))
        }
      }
      $1
      var forDebuggingOnly_auction_loss_url = undefined;
      var forDebuggingOnly_auction_win_url = undefined;
      const forDebuggingOnly = {};
      forDebuggingOnly.reportAdAuctionLoss = function(url){
        forDebuggingOnly_auction_loss_url = url;
      }
      forDebuggingOnly.reportAdAuctionWin = function(url){
        forDebuggingOnly_auction_win_url = url;
      }
      globalThis.forDebuggingOnly = forDebuggingOnly;

      try {
      generate_bid_response = await generateBid($0);
      if (generate_bid_response instanceof Array) {
        generate_bid_response = checkMultiBidLimit(generate_bid_response, multiBidLimit, featureFlags);

      // Add private aggregate contributions and debug urls to each response.
      // If the private aggregation is enabled, the contributions are expected to be set in ps_response.private_aggregation_contributions.
      generate_bid_response.forEach((response) => {
        if (featureFlags.enable_debug_url_generation &&
          (forDebuggingOnly_auction_loss_url
            || forDebuggingOnly_auction_win_url)) {
          response.debug_report_urls = {
            auction_debug_loss_url: forDebuggingOnly_auction_loss_url,
            auction_debug_win_url: forDebuggingOnly_auction_win_url
          };
        }
        if (ps_response.paapicontributions) {
          response.private_aggregation_contributions = ps_response.paapicontributions;
        }
      });
        ps_response.response = generate_bid_response;
      } else {
        // generateBid returned a single AdWithBid object.
        if (featureFlags.enable_debug_url_generation &&
          (forDebuggingOnly_auction_loss_url
            || forDebuggingOnly_auction_win_url)) {
          generate_bid_response.debug_report_urls = {
            auction_debug_loss_url: forDebuggingOnly_auction_loss_url,
            auction_debug_win_url: forDebuggingOnly_auction_win_url
          };
        }
        if (ps_response.paapicontributions) {
          generate_bid_response.private_aggregation_contributions = ps_response.paapicontributions;
        }
        ps_response.response.push(generate_bid_response);
      }
      } catch({error, message}) {
        if (featureFlags.enable_logging) {
          console.error("[Error: " + error + "; Message: " + message + "]");
        }
      }
      if (featureFlags.enable_logging) {
        return ps_response;
      }
      return ps_response.response;
    }
)JS_CODE";

// Wrapper Javascript over AdTech code.
// This wrapper supports the features below:
//- Exporting logs to Bidding Service using console.log
//- Hooks in wasm module
inline constexpr absl::string_view kPrepareDataForAdRetrievalEntryFunction =
    R"JS_CODE(
    async function $0EntryFunction(onDeviceEncodedSignalsHexString, $1, featureFlags){
      var ps_logs = [];
      var ps_errors = [];
      var ps_warns = [];
      if (featureFlags.enable_logging) {
        console.log = function(...args) {
          ps_logs.push(JSON.stringify(args))
        }
        console.error = function(...args) {
          ps_errors.push(JSON.stringify(args))
        }
        console.warn = function(...args) {
          ps_warns.push(JSON.stringify(args))
        }
      }
      const convertToUint8Array =
        (encodedOnDeviceSignalsIn) =>
          Uint8Array.from(encodedOnDeviceSignalsIn.match(/.{1,2}/g).map((byte) =>
            parseInt(byte, 16)));
      return {
        response: await $0(convertToUint8Array(onDeviceEncodedSignalsHexString), $1),
        logs: ps_logs,
        errors: ps_errors,
        warnings: ps_warns
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
