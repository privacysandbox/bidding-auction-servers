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

#ifndef SERVICES_BUYER_REPORTING_UDF_WRAPPER_H_
#define SERVICES_BUYER_REPORTING_UDF_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// The wrapper function which enables the below features for reportWin:
// - Exporting console.log, console.error and console.warn logs from UDF
// - sendReportTo() API to register the url to be pinged upon Auction win.
// - registerAdBeacon() API to register the interation reporting urls/ Fenced
//   frame reporting urls.
constexpr absl::string_view kReportWinWrapperFunction = R"JS_CODE(

function reportWinEntryFunction(
    auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
    directFromSellerSignals, enable_logging, $extraArgs) {
  const ps_report_win_response = {
    reportWinUrl: '',
    interactionReportingUrls: {},
    sendReportToInvoked: false,
    registerAdBeaconInvoked: false,
  };
  const ps_logs = [];
  const ps_errors = [];
  const ps_warns = [];
  if (enable_logging) {
    console.log = (...args) => ps_logs.push(JSON.stringify(args));
    console.warn = (...args) => ps_warns.push(JSON.stringify(args));
    console.error = (...args) => ps_errors.push(JSON.stringify(args));
  } else {
    console.log = console.warn = console.error = function() {};
  }
  globalThis.sendReportTo = function sendReportTo(url) {
    if (ps_report_win_response.sendReportToInvoked) {
      throw new Error('sendReportTo function invoked more than once');
    }
    ps_report_win_response.reportWinUrl = url;
    ps_report_win_response.sendReportToInvoked = true;
  };
  globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap) {
    if (ps_report_win_response.registerAdBeaconInvoked) {
      throw new Error(
          'registerAdBeaconInvoked function invoked more than once');
    }
    ps_report_win_response.interactionReportingUrls = eventUrlMap;
    ps_report_win_response.registerAdBeaconInvoked = true;
  };
  try {
    reportWin(
        auctionSignals, perBuyerSignals, signalsForWinner,
        buyerReportingSignals, directFromSellerSignals, $extraArgs);
  } catch (ex) {
    console.error(ex.message);
  }
  return {
    response: ps_report_win_response,
    buyerLogs: ps_logs,
    buyerErrors: ps_errors,
    buyerWarnings: ps_warns
  };
}
)JS_CODE";

// Returns the complete wrapped code for buyer.
// The function adds wrapper to the buyer provided reportWin() udf
// The wrapper supports:
// - Generation of event level reporting urls for buyer
// - Exporting console.logs from the AdTech execution.
std::string GetBuyerWrappedCode(absl::string_view buyer_js_code);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_REPORTING_UDF_WRAPPER_H_
