
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

constexpr absl::string_view kBuyerBaseCodeForPrivateAggregation = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }
    function generateBid(interest_group, auction_signals,buyer_signals,
                          trusted_bidding_signals,
                          device_signals){
    const bid = fibonacci(Math.floor(Math.random() * 30 + 1));
    console.log("Logging from generateBid");
    const contribution = {
      bucket: 100,
      value: 200,
    };
    privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
    return {
        render: "%s" + interest_group.adRenderIds[0],
        ad: {"arbitraryMetadataField": 1},
        bid: bid,
        allowComponentAuction: false
      };
    }
)JS_CODE";

constexpr absl::string_view kBuyerBaseCodeWithSignalValueAndSignalBucket =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }
    function generateBid(interest_group, auction_signals,buyer_signals,
                          trusted_bidding_signals,
                          device_signals){
    const bid = fibonacci(Math.floor(Math.random() * 30 + 1));
    console.log("Logging from generateBid");
    const contribution = {
      bucket: {baseValue:"winning-bid",scale:2.0, offset:1},
      value: {baseValue:"highest-scoring-other-bid",scale:3.0, offset:2},
    };
    privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
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

    function checkMultiBidLimit(generate_bid_response, multiBidLimit, enable_logging) {
      // Drop all the bids if generateBid response exceed multiBidLimit.
      if (generate_bid_response.length > multiBidLimit) {
        generate_bid_response = [];
        if (enable_logging) {
          console.error("[Error: more bids returned by generateBid than multiBidLimit]");
        }
      }
      return generate_bid_response;
    }

    function populateDebugReportingUrls(bid_response, paapicontributions, enable_url_generation, auction_loss_url, auction_win_url, enable_logging) {
      // Add private aggregate contributions and debug urls to each response.
      // If the private aggregation is enabled, the contributions are expected to be set in ps_response.private_aggregation_contributions.
      if (bid_response.length === 1) {
        if (enable_url_generation && ((typeof auction_loss_url === 'string') || (typeof auction_win_url === 'string'))) {
          bid_response[0].debug_report_urls = {
            auction_debug_loss_url: auction_loss_url,
            auction_debug_win_url: auction_win_url
          };
        }
        if (paapicontributions) {
          bid_response[0].private_aggregation_contributions = paapicontributions;
        }
      } else {
        let generate_loss_url = auction_loss_url instanceof Map;
        let generate_win_url = auction_win_url instanceof Map;
        if (enable_logging && enable_url_generation && (!generate_loss_url || !generate_win_url)) {
          console.log("Expected both loss / win report URLs to be of map instance; got, loss URLs of type: "+typeof auction_loss_url+" and win URLs of type: "+typeof auction_win_url);
        }
        bid_response.forEach((response) => {
          if (paapicontributions) {
            response.private_aggregation_contributions = paapicontributions;
          }
          if (enable_url_generation) {
            response.debug_report_urls = {
              auction_debug_loss_url: generate_loss_url ? auction_loss_url.get(response.render) : undefined,
              auction_debug_win_url: generate_win_url ? auction_win_url.get(response.render) : undefined
            };
          }
        });
      }
      return bid_response;
    }

    var ps_response = {
      response: [],
      logs: [],
      errors: [],
      warnings: []
    };

    async function generateBidEntryFunction(interest_group, auction_signals, buyer_signals, trusted_bidding_signals, device_signals, multiBidLimit, featureFlags){
      if (featureFlags.enable_logging){
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

      try {
        let response = await generateBid(interest_group, auction_signals, buyer_signals, trusted_bidding_signals, device_signals, multiBidLimit);
        let generate_bid_response = [];
        if (response instanceof Array) {
          generate_bid_response = checkMultiBidLimit(response, multiBidLimit, featureFlags.enable_logging);
        } else {
          generate_bid_response.push(response);
        }
        ps_response.response = populateDebugReportingUrls(generate_bid_response, ps_response.paapicontributions, featureFlags.enable_debug_url_generation, forDebuggingOnly_auction_loss_url, forDebuggingOnly_auction_win_url, featureFlags.enable_logging);
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

    function checkMultiBidLimit(generate_bid_response, multiBidLimit, enable_logging) {
      // Drop all the bids if generateBid response exceed multiBidLimit.
      if (generate_bid_response.length > multiBidLimit) {
        generate_bid_response = [];
        if (enable_logging) {
          console.error("[Error: more bids returned by generateBid than multiBidLimit]");
        }
      }
      return generate_bid_response;
    }

    function populateDebugReportingUrls(bid_response, paapicontributions, enable_url_generation, auction_loss_url, auction_win_url, enable_logging) {
      // Add private aggregate contributions and debug urls to each response.
      // If the private aggregation is enabled, the contributions are expected to be set in ps_response.private_aggregation_contributions.
      if (bid_response.length === 1) {
        if (enable_url_generation && ((typeof auction_loss_url === 'string') || (typeof auction_win_url === 'string'))) {
          bid_response[0].debug_report_urls = {
            auction_debug_loss_url: auction_loss_url,
            auction_debug_win_url: auction_win_url
          };
        }
        if (paapicontributions) {
          bid_response[0].private_aggregation_contributions = paapicontributions;
        }
      } else {
        let generate_loss_url = auction_loss_url instanceof Map;
        let generate_win_url = auction_win_url instanceof Map;
        if (enable_logging && enable_url_generation && (!generate_loss_url || !generate_win_url)) {
          console.log("Expected both loss / win report URLs to be of map instance; got, loss URLs of type: "+typeof auction_loss_url+" and win URLs of type: "+typeof auction_win_url);
        }
        bid_response.forEach((response) => {
          if (paapicontributions) {
            response.private_aggregation_contributions = paapicontributions;
          }
          if (enable_url_generation) {
            response.debug_report_urls = {
              auction_debug_loss_url: generate_loss_url ? auction_loss_url.get(response.render) : undefined,
              auction_debug_win_url: generate_win_url ? auction_win_url.get(response.render) : undefined
            };
          }
        });
      }
      return bid_response;
    }

    var ps_response = {
      response: [],
      logs: [],
      errors: [],
      warnings: []
    };

    async function generateBidEntryFunction(ads, sellerAuctionSignals, buyerSignals, preprocessedDataForRetrieval, encodedOnDeviceSignals, encodingVersion, multiBidLimit, featureFlags){
      if (featureFlags.enable_logging){
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

      try {
        let response = await generateBid(ads, sellerAuctionSignals, buyerSignals, preprocessedDataForRetrieval, encodedOnDeviceSignals, encodingVersion, multiBidLimit);
        let generate_bid_response = [];
        if (response instanceof Array) {
          generate_bid_response = checkMultiBidLimit(response, multiBidLimit, featureFlags.enable_logging);
        } else {
          generate_bid_response.push(response);
        }
        ps_response.response = populateDebugReportingUrls(generate_bid_response, ps_response.paapicontributions, featureFlags.enable_debug_url_generation, forDebuggingOnly_auction_loss_url, forDebuggingOnly_auction_win_url, featureFlags.enable_logging);
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

    async function prepareDataForAdRetrievalEntryFunction(onDeviceEncodedSignalsHexString, testArg, featureFlags){
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
        response: await prepareDataForAdRetrieval(convertToUint8Array(onDeviceEncodedSignalsHexString), testArg),
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
