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

constexpr char kReportWinEntryFunction[] = "reportWinEntryFunction";
inline constexpr char kEgressArgs[] =
    "egressVector, temporaryUnlimitedEgressVector";

// The wrapper function which enables the below features for reportWin:
// - Exporting console.log, console.error and console.warn logs from UDF
// - sendReportTo() API to register the url to be pinged upon Auction win.
// - registerAdBeacon() API to register the interation reporting urls/ Fenced
//   frame reporting urls.
constexpr absl::string_view kReportWinWrapperFunction = R"JS_CODE(
var ps_response = {
    response: {},
    logs: [],
    errors: [],
    warnings: []
  }
function reportWinEntryFunction(
    auctionConfig, perBuyerSignals, signalsForWinner, buyerReportingSignals,
    directFromSellerSignals, enable_logging, $extraArgs) {
  ps_sendReportToInvoked = false
  ps_registerAdBeaconInvoked = false
  ps_response.response = {
    reportWinUrl: '',
    interactionReportingUrls: {},
  };
  const ps_logs = [];
  const ps_errors = [];
  const ps_warns = [];
  if (enable_logging) {
    console.log = (...args) => ps_response.logs.push(JSON.stringify(args));
    console.warn = (...args) => ps_response.warnings.push(JSON.stringify(args));
    console.error = (...args) => ps_response.errors.push(JSON.stringify(args));
  } else {
    console.log = console.warn = console.error = function() {};
  }
  globalThis.sendReportTo = function sendReportTo(url) {
    if (ps_sendReportToInvoked) {
      throw new Error('sendReportTo function invoked more than once');
    }
    ps_response.response.reportWinUrl = url;
    ps_sendReportToInvoked = true;
  };
  globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap) {
    if (ps_registerAdBeaconInvoked) {
      throw new Error(
          'registerAdBeaconInvoked function invoked more than once');
    }
    ps_response.response.interactionReportingUrls = eventUrlMap;
    ps_registerAdBeaconInvoked = true;
  };
  try {
    reportWin(
        auctionConfig.auctionSignals, perBuyerSignals, signalsForWinner,
        buyerReportingSignals, directFromSellerSignals, $extraArgs);
  } catch (ex) {
    console.error(ex.message);
  }
  return ps_response;
}
)JS_CODE";

// Returns the complete wrapped code for buyer.
// The function adds wrapper to the buyer provided reportWin() udf
// The wrapper supports:
// - Generation of event level reporting urls for buyer
// - Exporting console.logs from the AdTech execution.
std::string GetBuyerWrappedCode(
    absl::string_view buyer_js_code, bool enable_protected_app_signals = false,
    bool enable_private_aggregate_reporting = false);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_REPORTING_UDF_WRAPPER_H_
