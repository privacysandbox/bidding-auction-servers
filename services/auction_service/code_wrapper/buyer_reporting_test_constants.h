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
#ifndef SERVICES_BUYER_REPORTING_TEST_CONSTANTS_H_
#define SERVICES_BUYER_REPORTING_TEST_CONSTANTS_H_

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {
constexpr absl::string_view kTestReportWinUdfWithValidation =
    R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        if(!buyerReportingSignals.seller){
          console.error("Missing seller in input to reportWin")
          return
        }
        if(!buyerReportingSignals.interestGroupName && !buyerReportingSignals.buyerReportingId){
          console.error("Missing both interestGroupName and buyerReportingId in input to reportWin")
          return
        }
        if(!buyerReportingSignals.adCost || buyerReportingSignals.adCost < 1){
          console.error("Missing adCost in input to reportWin")
          return
        }
        var reportWinUrl = "Invalid buyerReportingSignals"
        if(validateInputs(buyerReportingSignals)){
        reportWinUrl = "http://test.com?seller="+buyerReportingSignals.seller+
                    "&interestGroupName="+buyerReportingSignals.interestGroupName+
                    "&buyerReportingId="+buyerReportingSignals.buyerReportingId+
                    "&adCost="+buyerReportingSignals.adCost+
                    "&highestScoringOtherBid="+buyerReportingSignals.highestScoringOtherBid+
                    "&madeHighestScoringOtherBid="+buyerReportingSignals.madeHighestScoringOtherBid+
                    "&signalsForWinner="+JSON.stringify(signalsForWinner)+
                    "&perBuyerSignals="+perBuyerSignals+"&auctionSignals="+auctionSignals+"&desirability="+buyerReportingSignals.desirability+"&topLevelSeller="+buyerReportingSignals.topLevelSeller+"&modifiedBid="+buyerReportingSignals.modifiedBid;
        }
        console.log("Logging from ReportWin");
        console.error("Logging error from ReportWin")
        console.warn("Logging warning from ReportWin")
        sendReportTo(reportWinUrl)
        registerAdBeacon({"clickEvent":"http://click.com"})
    }
  function validateInputs(buyerReportingSignals){
    if(buyerReportingSignals.modelingSignals === undefined || buyerReportingSignals.modelingSignals<0 || buyerReportingSignals.modelingSignals>65535){
      return false
    }
    if(buyerReportingSignals.recency===undefined || buyerReportingSignals.recency<0){
      return false
    }
    if(buyerReportingSignals.joinCount===undefined || buyerReportingSignals.joinCount<0){
      return false
    }
    return true
  }
)JS_CODE";

constexpr absl::string_view kExpectedBuyerCodeWithReportWinForPA = R"JS_CODE(

function reportWinEntryFunction(
    auctionConfig, perBuyerSignals, signalsForWinner, buyerReportingSignals,
    directFromSellerSignals, enable_logging, $extraArgs) {
  ps_sendReportToInvoked = false
  ps_registerAdBeaconInvoked = false
  const ps_report_win_response = {
    reportWinUrl: '',
    interactionReportingUrls: {},
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
    if (ps_sendReportToInvoked) {
      throw new Error('sendReportTo function invoked more than once');
    }
    ps_report_win_response.reportWinUrl = url;
    ps_sendReportToInvoked = true;
  };
  globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap) {
    if (ps_registerAdBeaconInvoked) {
      throw new Error(
          'registerAdBeaconInvoked function invoked more than once');
    }
    ps_report_win_response.interactionReportingUrls = eventUrlMap;
    ps_registerAdBeaconInvoked = true;
  };
  try {
    reportWin(
        auctionConfig.auctionSignals, perBuyerSignals, signalsForWinner,
        buyerReportingSignals, directFromSellerSignals, $extraArgs);
  } catch (ex) {
    console.error(ex.message);
  }
  return {
    response: ps_report_win_response,
    logs: ps_logs,
    errors: ps_errors,
    warnings: ps_warns
  };
}
reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        if(!buyerReportingSignals.seller){
          console.error("Missing seller in input to reportWin")
          return
        }
        if(!buyerReportingSignals.interestGroupName && !buyerReportingSignals.buyerReportingId){
          console.error("Missing both interestGroupName and buyerReportingId in input to reportWin")
          return
        }
        if(!buyerReportingSignals.adCost || buyerReportingSignals.adCost < 1){
          console.error("Missing adCost in input to reportWin")
          return
        }
        var reportWinUrl = "Invalid buyerReportingSignals"
        if(validateInputs(buyerReportingSignals)){
        reportWinUrl = "http://test.com?seller="+buyerReportingSignals.seller+
                    "&interestGroupName="+buyerReportingSignals.interestGroupName+
                    "&buyerReportingId="+buyerReportingSignals.buyerReportingId+
                    "&adCost="+buyerReportingSignals.adCost+
                    "&highestScoringOtherBid="+buyerReportingSignals.highestScoringOtherBid+
                    "&madeHighestScoringOtherBid="+buyerReportingSignals.madeHighestScoringOtherBid+
                    "&signalsForWinner="+JSON.stringify(signalsForWinner)+
                    "&perBuyerSignals="+perBuyerSignals+"&auctionSignals="+auctionSignals+"&desirability="+buyerReportingSignals.desirability+"&topLevelSeller="+buyerReportingSignals.topLevelSeller+"&modifiedBid="+buyerReportingSignals.modifiedBid;
        }
        console.log("Logging from ReportWin");
        console.error("Logging error from ReportWin")
        console.warn("Logging warning from ReportWin")
        sendReportTo(reportWinUrl)
        registerAdBeacon({"clickEvent":"http://click.com"})
    }
  function validateInputs(buyerReportingSignals){
    if(buyerReportingSignals.modelingSignals === undefined || buyerReportingSignals.modelingSignals<0 || buyerReportingSignals.modelingSignals>65535){
      return false
    }
    if(buyerReportingSignals.recency===undefined || buyerReportingSignals.recency<0){
      return false
    }
    if(buyerReportingSignals.joinCount===undefined || buyerReportingSignals.joinCount<0){
      return false
    }
    return true
  }
)JS_CODE";

constexpr absl::string_view kExpectedBuyerCodeWithReportWinForPAS = R"JS_CODE(

function reportWinEntryFunction(
    auctionConfig, perBuyerSignals, signalsForWinner, buyerReportingSignals,
    directFromSellerSignals, enable_logging, egressVector, temporaryUnlimitedEgressVector) {
  ps_sendReportToInvoked = false
  ps_registerAdBeaconInvoked = false
  const ps_report_win_response = {
    reportWinUrl: '',
    interactionReportingUrls: {},
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
    if (ps_sendReportToInvoked) {
      throw new Error('sendReportTo function invoked more than once');
    }
    ps_report_win_response.reportWinUrl = url;
    ps_sendReportToInvoked = true;
  };
  globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap) {
    if (ps_registerAdBeaconInvoked) {
      throw new Error(
          'registerAdBeaconInvoked function invoked more than once');
    }
    ps_report_win_response.interactionReportingUrls = eventUrlMap;
    ps_registerAdBeaconInvoked = true;
  };
  try {
    reportWin(
        auctionConfig.auctionSignals, perBuyerSignals, signalsForWinner,
        buyerReportingSignals, directFromSellerSignals, egressVector, temporaryUnlimitedEgressVector);
  } catch (ex) {
    console.error(ex.message);
  }
  return {
    response: ps_report_win_response,
    logs: ps_logs,
    errors: ps_errors,
    warnings: ps_warns
  };
}
reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        if(!buyerReportingSignals.seller){
          console.error("Missing seller in input to reportWin")
          return
        }
        if(!buyerReportingSignals.interestGroupName && !buyerReportingSignals.buyerReportingId){
          console.error("Missing both interestGroupName and buyerReportingId in input to reportWin")
          return
        }
        if(!buyerReportingSignals.adCost || buyerReportingSignals.adCost < 1){
          console.error("Missing adCost in input to reportWin")
          return
        }
        var reportWinUrl = "Invalid buyerReportingSignals"
        if(validateInputs(buyerReportingSignals)){
        reportWinUrl = "http://test.com?seller="+buyerReportingSignals.seller+
                    "&interestGroupName="+buyerReportingSignals.interestGroupName+
                    "&buyerReportingId="+buyerReportingSignals.buyerReportingId+
                    "&adCost="+buyerReportingSignals.adCost+
                    "&highestScoringOtherBid="+buyerReportingSignals.highestScoringOtherBid+
                    "&madeHighestScoringOtherBid="+buyerReportingSignals.madeHighestScoringOtherBid+
                    "&signalsForWinner="+JSON.stringify(signalsForWinner)+
                    "&perBuyerSignals="+perBuyerSignals+"&auctionSignals="+auctionSignals+"&desirability="+buyerReportingSignals.desirability+"&topLevelSeller="+buyerReportingSignals.topLevelSeller+"&modifiedBid="+buyerReportingSignals.modifiedBid;
        }
        console.log("Logging from ReportWin");
        console.error("Logging error from ReportWin")
        console.warn("Logging warning from ReportWin")
        sendReportTo(reportWinUrl)
        registerAdBeacon({"clickEvent":"http://click.com"})
    }
  function validateInputs(buyerReportingSignals){
    if(buyerReportingSignals.modelingSignals === undefined || buyerReportingSignals.modelingSignals<0 || buyerReportingSignals.modelingSignals>65535){
      return false
    }
    if(buyerReportingSignals.recency===undefined || buyerReportingSignals.recency<0){
      return false
    }
    if(buyerReportingSignals.joinCount===undefined || buyerReportingSignals.joinCount<0){
      return false
    }
    return true
  }
)JS_CODE";

constexpr absl::string_view kProtectedAppSignalsBuyerBaseCode = R"JS_CODE(
reportWin = function(
    auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
    directFromSellerSignals, egressPayload, temporaryUnlimitedEgressPayload) {
  console.log('Testing Protected App Signals');
  var reportWinUrl = 'http://test.com?seller=' + buyerReportingSignals.seller +
      '&interestGroupName=' + buyerReportingSignals.interestGroupName +
      '&buyerReportingId=' + buyerReportingSignals.buyerReportingId +
      '&adCost=' + buyerReportingSignals.adCost + '&highestScoringOtherBid=' +
      buyerReportingSignals.highestScoringOtherBid +
      '&madeHighestScoringOtherBid=' +
      buyerReportingSignals.madeHighestScoringOtherBid +
      '&signalsForWinner=' + JSON.stringify(signalsForWinner) +
      '&perBuyerSignals=' + perBuyerSignals +
      '&auctionSignals=' + auctionSignals +
      '&desirability=' + buyerReportingSignals.desirability +
      '&topLevelSeller=' + buyerReportingSignals.topLevelSeller +
      '&modifiedBid=' + buyerReportingSignals.modifiedBid +
      '&egressPayload=' + egressPayload +
      '&temporaryUnlimitedEgressPayload=' + temporaryUnlimitedEgressPayload;
  console.log('Logging from ReportWin');
  console.error('Logging error from ReportWin');
  console.warn('Logging warning from ReportWin');
  sendReportTo(reportWinUrl);
  registerAdBeacon({'clickEvent': 'http://click.com'});
  return {
    'testSignal': 'testValue'
  };
}
)JS_CODE";

}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // SERVICES_BUYER_REPORTING_TEST_CONSTANTS_H_
