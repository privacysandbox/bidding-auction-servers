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

#ifndef FLEDGE_SERVICES_SELLER_CODE_WRAPPER_H_
#define FLEDGE_SERVICES_SELLER_CODE_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// The function that will be called first by Roma.
// The dispatch function name will be scoreAdEntryFunction.
// This wrapper supports the features below:
//- Exporting logs to Auction Service using console.log
constexpr absl::string_view kEntryFunction = R"JS_CODE(
    function scoreAdEntryFunction(adMetadata, bid, auctionConfig, trustedScoringSignals,
                                browserSignals, directFromSellerSignals, enable_logging){
    var ps_logs = [];
    if(enable_logging){
      console.log = function(...args) {
        ps_logs.push(JSON.stringify(args))
      }
    }
    scoreAdResponse = scoreAd(adMetadata, bid, auctionConfig, trustedScoringSignals,
                              browserSignals, directFromSellerSignals)
    return {
      response: scoreAdResponse,
      logs: ps_logs
    }
 }
)JS_CODE";

// The function that will be called by Roma to generate reporting urls.
// The dispatch function name will be reportingEntryFunction.
// This wrapper supports the features below:
//- Event level and fenced frame reporting for seller: reportResult() function
// execution
//- Event level and fenced frame reporting for buyer: reportWin() function
// execution
//- Exporting console.logs from the AdTech script execution
inline constexpr absl::string_view kReportingEntryFunction =
    R"JSCODE(
    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportingEntryFunction(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging) {
      var ps_report_result_response = {
        reportResultUrl : undefined,
        signalsForWinner : undefined,
        interactionReportingUrls : new Map(),
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
      var ps_logs = [];
      if(enable_logging){
        console.log = function(...args) {
          ps_logs.push(JSON.stringify(args))
        }
      }
      sendReportTo = function sendReportTo(url){
        if(!ps_report_result_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_result_response.reportResultUrl = url;
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(!ps_report_result_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        for (const [key, value] of eventUrlMap) {
          ps_report_result_response.interactionReportingUrls.set(key,value);
        }
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      ps_report_result_response.signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
      return {
        reportResultResponse: ps_report_result_response,
        sellerLogs: ps_logs,
      }
    }
)JSCODE";

// Returns the complete wrapped code for Seller.
// The function adds wrappers to the Seller provided script. This enables:
// - Generation of event level reporting urls for Seller
// - Generation of event level reporting urls for all the Buyers
// - Generation of event level debug reporting
// - Exporting console.logs from the AdTech execution.
std::string GetSellerWrappedCode(absl::string_view seller_js_code,
                                 bool enable_report_result_url_generation);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_SELLER_CODE_WRAPPER_H_
