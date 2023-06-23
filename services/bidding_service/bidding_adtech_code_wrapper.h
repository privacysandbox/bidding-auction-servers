//  Copyright 2022 Google LLC
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

#ifndef FLEDGE_SERVICES_BIDDING_SERVICE_ADTECH_CODE_WRAPPER_H_
#define FLEDGE_SERVICES_BIDDING_SERVICE_ADTECH_CODE_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view KGenerateBidWrapperJavascriptWasm = R"JSCODE(
    const forDebuggingOnly = {}
    forDebuggingOnly.auction_win_url = undefined;
    forDebuggingOnly.auction_loss_url = undefined;

    forDebuggingOnly.reportAdAuctionLoss = (url) => {
      forDebuggingOnly.auction_loss_url = url;
    }

    forDebuggingOnly.reportAdAuctionWin = (url) => {
      forDebuggingOnly.auction_win_url = url;
    }

    // Handler method to call adTech provided GenerateBid method and wrap the
    // response with debugging URLs.
    function generateBidWrapper( interest_group,
                                auction_signals,
                                buyer_signals,
                                trusted_bidding_signals,
                                device_signals) {

      var generateBidResponse = {};
      try {
        generateBidResponse = generateBid(interest_group, auction_signals,
          buyer_signals, trusted_bidding_signals, device_signals);
      } catch(ignored) {

      } finally {
        if( forDebuggingOnly.auction_win_url ||
                forDebuggingOnly.auction_loss_url ) {
          generateBidResponse.debug_report_urls = {
            auction_debug_loss_url: forDebuggingOnly.auction_loss_url,
            auction_debug_win_url: forDebuggingOnly.auction_win_url
          }
        }
      }
      return generateBidResponse;
    }
)JSCODE";

// Wraps the Ad Tech provided code for generate bid with wrapper code allowing
// Ad Techs to inject their debugging URLs for auction win and loss events.
std::string GetWrappedAdtechCodeForBidding(absl::string_view adtech_code_blob) {
  return absl::StrCat(KGenerateBidWrapperJavascriptWasm, adtech_code_blob);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_BIDDING_SERVICE_ADTECH_CODE_WRAPPER_H_
