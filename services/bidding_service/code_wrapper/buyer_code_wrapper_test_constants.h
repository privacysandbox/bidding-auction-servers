
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

    function generateBidEntryFunction(interest_group, auction_signals, buyer_signals, trusted_bidding_signals, device_signals, featureFlags){
      var ps_logs = [];
      var ps_errors = [];
      var ps_warns = [];
      if(featureFlags.enable_logging){
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
      device_signals.wasmHelper = globalWasmHelper;
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

      var generateBidResponse = {};
      try {
        generateBidResponse = generateBid(interest_group, auction_signals, buyer_signals, trusted_bidding_signals, device_signals);
      if( featureFlags.enable_debug_url_generation &&
             (forDebuggingOnly_auction_loss_url
                  || forDebuggingOnly_auction_win_url)) {
          generateBidResponse.debug_report_urls = {
            auction_debug_loss_url: forDebuggingOnly_auction_loss_url,
            auction_debug_win_url: forDebuggingOnly_auction_win_url
          }
        }
      } catch({error, message}) {
        if (featureFlags.enable_logging) {
          console.error("[Error: " + error + "; Message: " + message + "]");
        }
      }
      return {
        response: generateBidResponse !== undefined ? generateBidResponse : {},
        logs: ps_logs,
        errors: ps_errors,
        warnings: ps_warns
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
constexpr absl::string_view
    kExpectedProtectedAppSignalsGenerateBidCodeTemplate = R"JS_CODE(
  const globalWasmHex = [];
  const globalWasmHelper = globalWasmHex.length ? new WebAssembly.Module(Uint8Array.from(globalWasmHex)) : null;

    function generateBidEntryFunction(ads, sellerAuctionSignals, buyerSignals, preprocessedDataForRetrieval, encodedOnDeviceSignals, encodingVersion, featureFlags){
      var ps_logs = [];
      var ps_errors = [];
      var ps_warns = [];
      if(featureFlags.enable_logging){
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
      if (encodedOnDeviceSignals) {
        const convertToUint8Array =
          (encodedOnDeviceSignalsIn) =>
            Uint8Array.from(encodedOnDeviceSignalsIn.match(/.{1,2}/g).map((byte) =>
              parseInt(byte, 16)));
        console.log("PAS hex string: " + encodedOnDeviceSignals);
        encodedOnDeviceSignals = convertToUint8Array(encodedOnDeviceSignals);
        console.log("Uint8 PAS bytes: " + Array.apply([], encodedOnDeviceSignals).join(","));
      }
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

      var generateBidResponse = {};
      try {
        generateBidResponse = generateBid(ads, sellerAuctionSignals, buyerSignals, preprocessedDataForRetrieval, encodedOnDeviceSignals, encodingVersion);
      if( featureFlags.enable_debug_url_generation &&
             (forDebuggingOnly_auction_loss_url
                  || forDebuggingOnly_auction_win_url)) {
          generateBidResponse.debug_report_urls = {
            auction_debug_loss_url: forDebuggingOnly_auction_loss_url,
            auction_debug_win_url: forDebuggingOnly_auction_win_url
          }
        }
      } catch({error, message}) {
        if (featureFlags.enable_logging) {
          console.error("[Error: " + error + "; Message: " + message + "]");
        }
      }
      return {
        response: generateBidResponse !== undefined ? generateBidResponse : {},
        logs: ps_logs,
        errors: ps_errors,
        warnings: ps_warns
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
constexpr absl::string_view kExpectedPrepareDataForAdRetrievalTemplate =
    R"JS_CODE(
  const globalWasmHex = [];
  const globalWasmHelper = globalWasmHex.length ? new WebAssembly.Module(Uint8Array.from(globalWasmHex)) : null;

    function prepareDataForAdRetrievalEntryFunction(onDeviceEncodedSignalsHexString, testArg, featureFlags){
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
        response: prepareDataForAdRetrieval(convertToUint8Array(onDeviceEncodedSignalsHexString), testArg),
        logs: ps_logs,
        errors: ps_errors,
        warnings: ps_warns
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
