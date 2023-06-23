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

#ifndef FLEDGE_SERVICES_SELLER_ADTECH_REPORTING_WRAPPER_H_
#define FLEDGE_SERVICES_SELLER_ADTECH_REPORTING_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Wrapper Javascript Wasm for the reportResult function provided by Seller
// AdTech. This includes sendReport and registerAdBeacon functions.
inline constexpr absl::string_view kReportResultWrapperJavascriptWasm =
    R"JSCODE(
    var ps_report_result_response = {
      var signalsForWinner = undefined;
      var reportResultUrl = undefined;
      var interactionReportingUrls = new Map();
      var reportResultUrlGenerationErrorCount = 0;
      var sendReportToInvoked = false;
      var registerAdBeaconInvoked = false;
    }

    globalThis.sendReportTo = function sendReportTo(url){
      if(!ps_report_result_response.sendReportToInvoked) {
        throw new Exception("sendReportTo function invoked more than once");
      }
      ps_report_result_response.reportResultUrl = url;
      ps_report_result_response.sendReportToInvoked = true;
    }

    globalThis.registerAdBeacon =function registerAdBeacon(eventUrlMap){
      if(!ps_report_result_response.registerAdBeaconInvoked) {
        throw new Exception("registerAdBeaconInvoked function invoked more than once");
      }
      for (const [key, value] of eventUrlMap) {
        ps_report_result_response.interactionReportingUrls.set(key. value);
      }
      ps_report_result_response.sendReportToInvoked = true;
    }

    // Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportResultWrapper(auctionConfig, sellerReportingSignals) {
      var reportResultResponse = {};
      var signalsForWinner = "";
      try {
        signalsForWinner = reportResult(auctionConfig, sellerReportingSignals);
      } catch(ignored) {
        ps_report_result_response.reportResultUrlGenerationErrorCount = 1;
      }
      ps_report_result_response.signalsForWinner = signalsForWinner;
      return ps_report_result_response;
)JSCODE";

// Wraps the Ad Tech provided code for reportResult with wrapper code allowing
// Ad Techs to send URLs for event level and fenced frame reporting.
std::string GetWrappedAdtechCodeForReportResult(
    absl::string_view adtech_code_blob) {
  return absl::StrCat(KReportResultWrapperJavascriptWasm, adtech_code_blob);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_SELLER_ADTECH_REPORTING_WRAPPER_H_
