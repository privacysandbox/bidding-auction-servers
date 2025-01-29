//  Copyright 2024 Google LLC
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

#ifndef SERVICES_AUCTION_SERVICE_SELLER_UDF_WRAPPER_H_
#define SERVICES_AUCTION_SERVICE_SELLER_UDF_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr char kReportResultEntryFunction[] = "reportResultEntryFunction";

// The function that will be called first by Roma.
// The dispatch function name will be scoreAdEntryFunction.
// This wrapper supports the features below:
//- Exporting logs to Auction Service using console.log
constexpr absl::string_view kEntryFunction = R"JS_CODE(
    var ps_response = {
        response: {},
        logs: [],
        errors: [],
        warnings: []
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

    function scoreAdEntryFunction(adMetadata, bid, auctionConfig, trustedScoringSignals,
                                browserSignals, directFromSellerSignals, featureFlags){
    if (featureFlags.enable_logging) {
        console.log = (...args) => ps_response.logs.push(JSON.stringify(args));
        console.warn = (...args) => ps_response.warnings.push(JSON.stringify(args));
        console.error = (...args) => ps_response.errors.push(JSON.stringify(args));
    } else {
      console.log = console.warn = console.error = function() {};
    }
      var scoreAdResponse = {};
      try {
        ps_response.response = scoreAd(adMetadata, bid, auctionConfig,
              trustedScoringSignals, browserSignals, directFromSellerSignals);
      } catch({error, message}) {
          console.error("[Error: " + error + "; Message: " + message + "]");
      } finally {
        if( featureFlags.enable_debug_url_generation &&
              (forDebuggingOnly_auction_loss_url
                  || forDebuggingOnly_auction_win_url)) {
          ps_response.response.debugReportUrls = {
            auctionDebugLossUrl: forDebuggingOnly_auction_loss_url,
            auctionDebugWinUrl: forDebuggingOnly_auction_win_url
          }
        }
      }
      return ps_response;
    }
)JS_CODE";

inline constexpr absl::string_view kReportResultWrapperFunction =
    R"JS_CODE(
    var ps_response = {
      response: {},
      logs: [],
      errors: [],
      warnings: []
    }
    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportResultEntryFunction(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging) {
    ps_sendReportTo_invoked = false
    ps_registerAdBeacon_invoked = false
    ps_response.response = {
        signalsForWinner : "null",
        reportResultUrl : "",
        interactionReportingUrls : {},
      }
    if (enable_logging) {
        console.log = (...args) => ps_response.logs.push(JSON.stringify(args));
        console.warn = (...args) => ps_response.warnings.push(JSON.stringify(args));
        console.error = (...args) => ps_response.errors.push(JSON.stringify(args));
    } else {
      console.log = console.warn = console.error = function() {};
    }
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_sendReportTo_invoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_response.response.reportResultUrl = url;
        ps_sendReportTo_invoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_registerAdBeacon_invoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_response.response.interactionReportingUrls=eventUrlMap;
        ps_registerAdBeacon_invoked = true;
      }
      try{
        signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
        ps_response.response.signalsForWinner = JSON.stringify(signalsForWinner)
      } catch(ex){
        console.error(ex.message)
      }
      return ps_response;
    }
)JS_CODE";

// Returns the complete wrapped code for Seller.
// The function adds wrappers to the Seller provided scoreAd and reportResult
// UDF. The wrapper supports:
// - Generation of event level reporting urls for Seller
// - Generation of event level debug reporting
// - Exporting console.logs from the AdTech execution.
std::string GetSellerWrappedCode(absl::string_view seller_js_code,
                                 bool enable_report_result_url_generation,
                                 bool enable_private_aggregate_reporting);
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_SELLER_UDF_WRAPPER_H_
