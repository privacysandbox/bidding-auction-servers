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

#ifndef FLEDGE_SERVICES_AUCTION_SERVICE_ADTECH_CODE_WRAPPER_H_
#define FLEDGE_SERVICES_AUCTION_SERVICE_ADTECH_CODE_WRAPPER_H_

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Wrapper Javascript over AdTech code to capture debugging URLs.
constexpr absl::string_view KScoreAdWrapperJavascriptWasm = R"JS_CODE(
    const forDebuggingOnly = {}
    forDebuggingOnly.auction_win_url = undefined;
    forDebuggingOnly.auction_loss_url = undefined;

    forDebuggingOnly.reportAdAuctionLoss = (url) => {
      forDebuggingOnly.auction_loss_url = url;
    }

    forDebuggingOnly.reportAdAuctionWin = (url) => {
      forDebuggingOnly.auction_win_url = url;
    }

    // Handler method to call adTech provided ScoreAd method and wrap the
    // response with debugging URLs.
    function scoreAdWrapper( ad_metadata,
                             bid,
                             auction_config,
                             scoring_signals,
                             device_signals,
                             direct_from_seller_signals) {
      var scoreAdResponse = {};
      try {
        scoreAdResponse = scoreAd(ad_metadata, bid, auction_config,
              scoring_signals, device_signals, direct_from_seller_signals);
      } catch(ignored) {

      } finally {
        if( forDebuggingOnly.auction_win_url ||
                forDebuggingOnly.auction_loss_url ) {
          scoreAdResponse.debug_report_urls = {
            auction_debug_loss_url: forDebuggingOnly.auction_loss_url,
            auction_debug_win_url: forDebuggingOnly.auction_win_url
          }
        }
      }
      return scoreAdResponse;
    }
)JS_CODE";

// Method to concatinate wrapper code with ad-tech code.
std::string GetWrappedAdtechCodeForScoring(absl::string_view ad_tech_code) {
  return absl::StrCat(KScoreAdWrapperJavascriptWasm, ad_tech_code);
}

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_AUCTION_SERVICE_ADTECH_CODE_WRAPPER_H_
