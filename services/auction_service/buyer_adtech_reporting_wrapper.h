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

#ifndef FLEDGE_SERVICES_BUYER_ADTECH_REPORTING_WRAPPER_H_
#define FLEDGE_SERVICES_BUYER_ADTECH_REPORTING_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Wrapper Javascript Wasm for the reportWin function provided by Buyer AdTech.
// This includes sendReport and registerAdBeacon functions.
inline constexpr absl::string_view kReportWinWrapperJavascriptWasm = R"JSCODE(
    var ps_report_win_response = {
      var reportWintUrl = undefined;
      var interactionReportingUrls = new Map();
      var reportWinUrlGenerationErrorCount = 0;
      var sendReportToInvoked = false;
      var registerAdBeaconInvoked = false;
    }

    globalThis.sendReportTo = function sendReportTo(url){
      if(!ps_report_win_response.sendReportToInvoked) {
        throw new Exception("sendReportTo function invoked more than once");
      }
      ps_report_win_response.reportWinUrl = url;
      ps_report_win_response.sendReportToInvoked = true;
    }

    globalThis.registerAdBeacon =function registerAdBeacon(eventUrlMap){
      if(!ps_report_win_response.registerAdBeaconInvoked) {
        throw new Exception("registerAdBeaconInvoked function invoked more than once");
      }
      for (const [key, value] of eventUrlMap) {
        ps_report_win_response.interactionReportingUrls.set(key. value);
      }
      ps_report_win_response.sendReportToInvoked = true;
    }

    // Handler method to call adTech provided reportWin method and wrap the
    // response with reportWin url and interaction reporting urls.
    function reportWinWrapper(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals) {
      var reportWinResponse = {};
      try {
        reportWin(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals);
      } catch(ignored) {
        ps_report_win_response.reportWinUrlGenerationErrorCount = 1;
      }
      return ps_report_win_response;
)JSCODE";

// Wraps the Ad Tech provided code for reportWin with wrapper code allowing
// Ad Techs to send URLs for event level and fenced frame reporting.
std::string GetWrappedAdtechCodeForReportWin(
    absl::string_view adtech_code_blob) {
  return absl::StrCat(kReportWinWrapperJavascriptWasm, adtech_code_blob);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BUYER_ADTECH_REPORTING_WRAPPER_H_
