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
#ifndef FLEDGE_SERVICES_SELLER_CODE_WRAPPER_TEST_CONSTANTS_H_
#define FLEDGE_SERVICES_SELLER_CODE_WRAPPER_TEST_CONSTANTS_H_

#include "absl/strings/string_view.h"
#include "services/auction_service/auction_test_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kBuyerOrigin[] = "http://buyer1.com";
constexpr char kTestComponentReportResultUrlWithEverything[] =
    "http://"
    "test.com&topLevelSeller=testTopLevelSeller&bid=1&bidCurrency=EUR&"
    "dataVersion="
    "1989&modifiedBid="
    "2&modifiedBidCurrency=USD&"
    "highestScoringOtherBid=1&highestScoringOtherBidCurrency=???&"
    "topWindowHostname=fenceStreetJournal.com&interestGroupOwner="
    "barStandardAds.com";
constexpr char kTestComponentReportResultUrlWithNoModifiedBid[] =
    "http://test.com&topLevelSeller=testTopLevelSeller&bid=1&modifiedBid=1";
constexpr absl::string_view kBuyerBaseCodeSimple =
    R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
})JS_CODE";

constexpr absl::string_view kBuyerBaseCodeComponentAuction =
    R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        sendReportTo("http://test.com")
        registerAdBeacon({"clickEvent":"http://click.com"})
})JS_CODE";

constexpr absl::string_view kBuyerBaseCode =
    R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        var test_render_url = buyerReportingSignals.renderUrl
        var test_render_url = buyerReportingSignals.renderURL
        if(buyerReportingSignals.seller==null || buyerReportingSignals.seller == undefined || buyerReportingSignals.seller == ""){
          console.error("Missing seller in input to reportWin")
          return
        }
        if(buyerReportingSignals.adCost == 0 || buyerReportingSignals.adCost == -1
            || buyerReportingSignals.adCost == undefined
            || buyerReportingSignals.adCost == null){
          console.error("Missing adCost in input to reportWin")
          return
        }
        var reportWinUrl = "http://test.com?seller="+buyerReportingSignals.seller+
                    "&adCost="+buyerReportingSignals.adCost+
                    "&modelingSignals="+buyerReportingSignals.modelingSignals+
                    "&recency="+buyerReportingSignals.recency+
                    "&madeHighestScoringOtherBid="+buyerReportingSignals.madeHighestScoringOtherBid+
                    "&joinCount="+buyerReportingSignals.joinCount+
                    "&signalsForWinner="+JSON.stringify(signalsForWinner)+
                    "&perBuyerSignals="+perBuyerSignals+
                    "&auctionSignals="+auctionSignals+
                    "&desirability="+buyerReportingSignals.desirability+
                    "&dataVersion="+buyerReportingSignals.dataVersion
        if(buyerReportingSignals.hasOwnProperty("buyerReportingId")){
            reportWinUrl = reportWinUrl+"&buyerReportingId="+buyerReportingSignals.buyerReportingId
        }
        console.log("Logging from ReportWin");
        console.error("Logging error from ReportWin")
        console.warn("Logging warning from ReportWin")
        sendReportTo(reportWinUrl)
        registerAdBeacon({"clickEvent":"http://click.com"})
    }
)JS_CODE";

constexpr absl::string_view kBuyerBaseCodeWithValidation =
    R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        if(!buyerReportingSignals.seller){
          console.error("Missing seller in input to reportWin")
          return
        }
        if(!buyerReportingSignals.interestGroupName){
          console.error("Missing interestGroupName in input to reportWin")
          return
        }
        if(buyerReportingSignals.adCost === undefined || buyerReportingSignals.adCost === 0 || buyerReportingSignals.adCost === -1){
          console.error("Missing adCost in input to reportWin")
          return
        }
        var reportWinUrl = "Invalid buyerReportingSignals"
        if(validateInputs(buyerReportingSignals)){
        reportWinUrl = "http://test.com?seller="+buyerReportingSignals.seller+
                    "&interestGroupName="+buyerReportingSignals.interestGroupName+
                    "&adCost="+buyerReportingSignals.adCost+
                    "&madeHighestScoringOtherBid="+buyerReportingSignals.madeHighestScoringOtherBid+
                    "&signalsForWinner="+JSON.stringify(signalsForWinner)+
                    "&perBuyerSignals="+perBuyerSignals+
                    "&auctionSignals="+auctionSignals+
                    "&desirability="+buyerReportingSignals.desirability+
                    "&dataVersion="+buyerReportingSignals.dataVersion;
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

constexpr absl::string_view kProtectedAppSignalsBuyerBaseCode =
    R"JS_CODE(reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, egressPayload, temporaryUnlimitedEgressPayload){
      console.log("Testing Protected App Signals");
      sendReportTo("http://test.com");
      registerAdBeacon({"clickEvent":"http://click.com"});
      return {"testSignal":"testValue"}
    }
)JS_CODE";

// &dataVersion="+sellerReportingSignals.dataVersion

constexpr absl::string_view kComponentAuctionCode = R"JS_CODE(
    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      return {
        ad: bid_metadata["topLevelSeller"],
        desirability: 1,
        bid: 2,
        incomingBidInSellerCurrency: 1.868,
        bidCurrency: "USD",
        allowComponentAuction: true
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner)
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&modifiedBid="+sellerReportingSignals.modifiedBid+
          "&modifiedBidCurrency="+sellerReportingSignals.modifiedBidCurrency+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }

)JS_CODE";

constexpr absl::string_view kTopLevelSellerCode = R"JS_CODE(
    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, device_signals, directFromSellerSignals){
      return {
        ad: device_signals["topLevelSeller"],
        desirability: 1,
        bid: 2,
        allowComponentAuction: true
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.componentSeller === undefined || sellerReportingSignals.componentSeller.length === 0){
          sendReportTo("http://test.com")
        } else{
          sendReportTo("http://test.com&componentSeller="+sellerReportingSignals.componentSeller+"&bid="+sellerReportingSignals.bid+"&desirability="+sellerReportingSignals.desirability)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }

)JS_CODE";

constexpr absl::string_view kComponentAuctionCodeWithNoModifiedBid = R"JS_CODE(
    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      return {
        ad: bid_metadata["topLevelSeller"],
        desirability: 1,
        allowComponentAuction: true
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com")
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&bid="+sellerReportingSignals.bid+"&modifiedBid="+sellerReportingSignals.modifiedBid)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }

)JS_CODE";

constexpr absl::string_view kSkipAdComponentAuctionCode = R"JS_CODE(
    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      return {
        ad: bid_metadata["topLevelSeller"],
        desirability: 1,
        bid: 2,
        allowComponentAuction: false
      }
    }
)JS_CODE";

constexpr absl::string_view kTopLevelAuctionCode = R"JS_CODE(
    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      let desirability = 0;

      if (bid_metadata["componentSeller"] !== undefined &&
       bid_metadata["componentSeller"].length > 0) {
        desirability = 1;
      }
      return {
        ad: bid_metadata["componentSeller"],
        desirability: desirability,
        incomingBidInSellerCurrency: 1.868,
        bidCurrency: "USD",
        bid: bid,
        allowComponentAuction: true
      }
    }
)JS_CODE";

constexpr absl::string_view kExpectedFinalCode = R"JS_CODE(
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

    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportingEntryFunction(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging, buyerReportingMetadata, ) {
    ps_signalsForWinner = ""
    const ps_report_result_response = {
        reportResultUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
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
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_result_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_result_response.reportResultUrl = url;
        ps_report_result_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_result_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_result_response.interactionReportingUrls=eventUrlMap;
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      ps_signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
      try{
      if(buyerReportingMetadata.enableReportWinUrlGeneration){
        var buyerOrigin = buyerReportingMetadata.buyerOrigin
        var functionSuffix = buyerOrigin.replace(/[^a-zA-Z0-9 ]/g, "")
        var auctionSignals = auctionConfig.auctionSignals
        var buyerReportingSignals = sellerReportingSignals
        delete buyerReportingSignals.desirability
        if(buyerReportingMetadata.hasOwnProperty("interestGroupName")){
          buyerReportingSignals.interestGroupName = buyerReportingMetadata.interestGroupName
        }
        buyerReportingSignals.madeHighestScoringOtherBid = buyerReportingMetadata.madeHighestScoringOtherBid
        buyerReportingSignals.joinCount = buyerReportingMetadata.joinCount
        buyerReportingSignals.recency = buyerReportingMetadata.recency
        buyerReportingSignals.modelingSignals = buyerReportingMetadata.modelingSignals
        perBuyerSignals = buyerReportingMetadata.perBuyerSignals
        buyerReportingSignals.seller = buyerReportingMetadata.seller
        buyerReportingSignals.adCost = buyerReportingMetadata.adCost
        buyerReportingSignals.dataVersion = buyerReportingMetadata.dataVersion
        if(buyerReportingMetadata.hasOwnProperty("buyerReportingId")){
          buyerReportingSignals.buyerReportingId = buyerReportingMetadata.buyerReportingId
        }
        // Absence of interest group indicates that this is a protected app
        // signals ad.
        if (buyerReportingMetadata.enableProtectedAppSignals &&
            (buyerReportingSignals.interestGroupName === null ||
             buyerReportingSignals.interestGroupName.trim() === "")) {
          functionSuffix += "ProtectedAppSignals";
        }
        var reportWinFunction = "reportWinWrapper"+functionSuffix+"(auctionSignals, perBuyerSignals, ps_signalsForWinner, buyerReportingSignals,"+
                              "directFromSellerSignals, enable_logging, )"
        var reportWinResponse = eval(reportWinFunction)
        return {
          reportResultResponse: ps_report_result_response,
          sellerLogs: ps_logs,
          sellerErrors: ps_errors,
          sellerWarnings: ps_warns,
          reportWinResponse: reportWinResponse.response,
          buyerLogs: reportWinResponse.buyerLogs,
          buyerErrors: reportWinResponse.buyerErrors,
          buyerWarnings: reportWinResponse.buyerWarnings,
      }
      }
      } catch(ex){
        console.error(ex.message)
      }
      return {
        reportResultResponse: ps_report_result_response,
        sellerLogs: ps_logs,
        sellerErrors: ps_errors,
        sellerWarnings: ps_warns,
      }
    }

    // Handler method to call adTech provided reportWin method and wrap the
    // response with reportWin url and interaction reporting urls.
    function reportWinWrapperhttpbuyer1com(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, enable_logging, ) {
      const ps_report_win_response = {
        reportWinUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
      const ps_buyer_logs = [];
      const ps_buyer_error_logs = [];
      const ps_buyer_warning_logs = [];
    if (enable_logging) {
        console.log = (...args) => ps_buyer_logs.push(JSON.stringify(args));
        console.warn = (...args) => ps_buyer_warning_logs.push(JSON.stringify(args));
        console.error = (...args) => ps_buyer_error_logs.push(JSON.stringify(args));
    } else {
      console.log = console.warn = console.error = function() {};
    }
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_win_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_win_response.reportWinUrl = url;
        ps_report_win_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_win_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_win_response.interactionReportingUrls = eventUrlMap;
        ps_report_win_response.registerAdBeaconInvoked = true;
      }
      {
      reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
        var test_render_url = buyerReportingSignals.renderUrl
        var test_render_url = buyerReportingSignals.renderURL
        if(buyerReportingSignals.seller==null || buyerReportingSignals.seller == undefined || buyerReportingSignals.seller == ""){
          console.error("Missing seller in input to reportWin")
          return
        }
        if(buyerReportingSignals.adCost == 0 || buyerReportingSignals.adCost == -1
            || buyerReportingSignals.adCost == undefined
            || buyerReportingSignals.adCost == null){
          console.error("Missing adCost in input to reportWin")
          return
        }
        var reportWinUrl = "http://test.com?seller="+buyerReportingSignals.seller+
                    "&adCost="+buyerReportingSignals.adCost+
                    "&modelingSignals="+buyerReportingSignals.modelingSignals+
                    "&recency="+buyerReportingSignals.recency+
                    "&madeHighestScoringOtherBid="+buyerReportingSignals.madeHighestScoringOtherBid+
                    "&joinCount="+buyerReportingSignals.joinCount+
                    "&signalsForWinner="+JSON.stringify(signalsForWinner)+
                    "&perBuyerSignals="+perBuyerSignals+
                    "&auctionSignals="+auctionSignals+
                    "&desirability="+buyerReportingSignals.desirability+
                    "&dataVersion="+buyerReportingSignals.dataVersion
        if(buyerReportingSignals.hasOwnProperty("buyerReportingId")){
            reportWinUrl = reportWinUrl+"&buyerReportingId="+buyerReportingSignals.buyerReportingId
        }
        console.log("Logging from ReportWin");
        console.error("Logging error from ReportWin")
        console.warn("Logging warning from ReportWin")
        sendReportTo(reportWinUrl)
        registerAdBeacon({"clickEvent":"http://click.com"})
    }

      }
      try{
      reportWin(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, )
      } catch(ex){
        console.error(ex.message)
      }
      return {
        response: ps_report_win_response,
        buyerLogs: ps_buyer_logs,
        buyerErrors: ps_buyer_error_logs,
        buyerWarnings: ps_buyer_warning_logs
      }
    }

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd")
      console.error("Logging error from ScoreAd")
      console.warn("Logging warn from ScoreAd")
      return {
        desirability: score,
        allow_component_auction: false
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner+
          "&buyerAndSellerReportingId="+sellerReportingSignals.buyerAndSellerReportingId+
          "&selectedBuyerAndSellerReportingId="+sellerReportingSignals.selectedBuyerAndSellerReportingId)
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }
)JS_CODE";

constexpr absl::string_view kExpectedProtectedAppSignalsFinalCode = R"JS_CODE(
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

    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportingEntryFunctionProtectedAppSignals(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging, buyerReportingMetadata, egressPayload, temporaryUnlimitedEgressPayload) {
    ps_signalsForWinner = ""
    const ps_report_result_response = {
        reportResultUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
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
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_result_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_result_response.reportResultUrl = url;
        ps_report_result_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_result_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_result_response.interactionReportingUrls=eventUrlMap;
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      ps_signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
      try{
      if(buyerReportingMetadata.enableReportWinUrlGeneration){
        var buyerOrigin = buyerReportingMetadata.buyerOrigin
        var functionSuffix = buyerOrigin.replace(/[^a-zA-Z0-9 ]/g, "")
        var auctionSignals = auctionConfig.auctionSignals
        var buyerReportingSignals = sellerReportingSignals
        delete buyerReportingSignals.desirability
        if(buyerReportingMetadata.hasOwnProperty("interestGroupName")){
          buyerReportingSignals.interestGroupName = buyerReportingMetadata.interestGroupName
        }
        buyerReportingSignals.madeHighestScoringOtherBid = buyerReportingMetadata.madeHighestScoringOtherBid
        buyerReportingSignals.joinCount = buyerReportingMetadata.joinCount
        buyerReportingSignals.recency = buyerReportingMetadata.recency
        buyerReportingSignals.modelingSignals = buyerReportingMetadata.modelingSignals
        perBuyerSignals = buyerReportingMetadata.perBuyerSignals
        buyerReportingSignals.seller = buyerReportingMetadata.seller
        buyerReportingSignals.adCost = buyerReportingMetadata.adCost
        buyerReportingSignals.dataVersion = buyerReportingMetadata.dataVersion
        if(buyerReportingMetadata.hasOwnProperty("buyerReportingId")){
          buyerReportingSignals.buyerReportingId = buyerReportingMetadata.buyerReportingId
        }
        // Absence of interest group indicates that this is a protected app
        // signals ad.
        if (buyerReportingMetadata.enableProtectedAppSignals &&
            (buyerReportingSignals.interestGroupName === null ||
             buyerReportingSignals.interestGroupName.trim() === "")) {
          functionSuffix += "ProtectedAppSignals";
        }
        var reportWinFunction = "reportWinWrapper"+functionSuffix+"(auctionSignals, perBuyerSignals, ps_signalsForWinner, buyerReportingSignals,"+
                              "directFromSellerSignals, enable_logging, egressPayload, temporaryUnlimitedEgressPayload)"
        var reportWinResponse = eval(reportWinFunction)
        return {
          reportResultResponse: ps_report_result_response,
          sellerLogs: ps_logs,
          sellerErrors: ps_errors,
          sellerWarnings: ps_warns,
          reportWinResponse: reportWinResponse.response,
          buyerLogs: reportWinResponse.buyerLogs,
          buyerErrors: reportWinResponse.buyerErrors,
          buyerWarnings: reportWinResponse.buyerWarnings,
      }
      }
      } catch(ex){
        console.error(ex.message)
      }
      return {
        reportResultResponse: ps_report_result_response,
        sellerLogs: ps_logs,
        sellerErrors: ps_errors,
        sellerWarnings: ps_warns,
      }
    }

    // Handler method to call adTech provided reportWin method and wrap the
    // response with reportWin url and interaction reporting urls.
    function reportWinWrapperhttpbuyer1comProtectedAppSignals(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, enable_logging, egressPayload, temporaryUnlimitedEgressPayload) {
      const ps_report_win_response = {
        reportWinUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
      const ps_buyer_logs = [];
      const ps_buyer_error_logs = [];
      const ps_buyer_warning_logs = [];
    if (enable_logging) {
        console.log = (...args) => ps_buyer_logs.push(JSON.stringify(args));
        console.warn = (...args) => ps_buyer_warning_logs.push(JSON.stringify(args));
        console.error = (...args) => ps_buyer_error_logs.push(JSON.stringify(args));
    } else {
      console.log = console.warn = console.error = function() {};
    }
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_win_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_win_response.reportWinUrl = url;
        ps_report_win_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_win_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_win_response.interactionReportingUrls = eventUrlMap;
        ps_report_win_response.registerAdBeaconInvoked = true;
      }
      {
      reportWinProtectedAppSignals = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, egressPayload, temporaryUnlimitedEgressPayload){
      console.log("Testing Protected App Signals");
      sendReportTo("http://test.com");
      registerAdBeacon({"clickEvent":"http://click.com"});
      return {"testSignal":"testValue"}
    }

      }
      try{
      reportWinProtectedAppSignals(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, egressPayload, temporaryUnlimitedEgressPayload)
      } catch(ex){
        console.error(ex.message)
      }
      return {
        response: ps_report_win_response,
        buyerLogs: ps_buyer_logs,
        buyerErrors: ps_buyer_error_logs,
        buyerWarnings: ps_buyer_warning_logs
      }
    }

    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportingEntryFunction(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging, buyerReportingMetadata, ) {
    ps_signalsForWinner = ""
    const ps_report_result_response = {
        reportResultUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
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
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_result_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_result_response.reportResultUrl = url;
        ps_report_result_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_result_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_result_response.interactionReportingUrls=eventUrlMap;
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      ps_signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
      try{
      if(buyerReportingMetadata.enableReportWinUrlGeneration){
        var buyerOrigin = buyerReportingMetadata.buyerOrigin
        var functionSuffix = buyerOrigin.replace(/[^a-zA-Z0-9 ]/g, "")
        var auctionSignals = auctionConfig.auctionSignals
        var buyerReportingSignals = sellerReportingSignals
        delete buyerReportingSignals.desirability
        if(buyerReportingMetadata.hasOwnProperty("interestGroupName")){
          buyerReportingSignals.interestGroupName = buyerReportingMetadata.interestGroupName
        }
        buyerReportingSignals.madeHighestScoringOtherBid = buyerReportingMetadata.madeHighestScoringOtherBid
        buyerReportingSignals.joinCount = buyerReportingMetadata.joinCount
        buyerReportingSignals.recency = buyerReportingMetadata.recency
        buyerReportingSignals.modelingSignals = buyerReportingMetadata.modelingSignals
        perBuyerSignals = buyerReportingMetadata.perBuyerSignals
        buyerReportingSignals.seller = buyerReportingMetadata.seller
        buyerReportingSignals.adCost = buyerReportingMetadata.adCost
        buyerReportingSignals.dataVersion = buyerReportingMetadata.dataVersion
        if(buyerReportingMetadata.hasOwnProperty("buyerReportingId")){
          buyerReportingSignals.buyerReportingId = buyerReportingMetadata.buyerReportingId
        }
        // Absence of interest group indicates that this is a protected app
        // signals ad.
        if (buyerReportingMetadata.enableProtectedAppSignals &&
            (buyerReportingSignals.interestGroupName === null ||
             buyerReportingSignals.interestGroupName.trim() === "")) {
          functionSuffix += "ProtectedAppSignals";
        }
        var reportWinFunction = "reportWinWrapper"+functionSuffix+"(auctionSignals, perBuyerSignals, ps_signalsForWinner, buyerReportingSignals,"+
                              "directFromSellerSignals, enable_logging, )"
        var reportWinResponse = eval(reportWinFunction)
        return {
          reportResultResponse: ps_report_result_response,
          sellerLogs: ps_logs,
          sellerErrors: ps_errors,
          sellerWarnings: ps_warns,
          reportWinResponse: reportWinResponse.response,
          buyerLogs: reportWinResponse.buyerLogs,
          buyerErrors: reportWinResponse.buyerErrors,
          buyerWarnings: reportWinResponse.buyerWarnings,
      }
      }
      } catch(ex){
        console.error(ex.message)
      }
      return {
        reportResultResponse: ps_report_result_response,
        sellerLogs: ps_logs,
        sellerErrors: ps_errors,
        sellerWarnings: ps_warns,
      }
    }

    // Handler method to call adTech provided reportWin method and wrap the
    // response with reportWin url and interaction reporting urls.
    function reportWinWrapperhttpbuyer1com(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, enable_logging, ) {
      const ps_report_win_response = {
        reportWinUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
      const ps_buyer_logs = [];
      const ps_buyer_error_logs = [];
      const ps_buyer_warning_logs = [];
    if (enable_logging) {
        console.log = (...args) => ps_buyer_logs.push(JSON.stringify(args));
        console.warn = (...args) => ps_buyer_warning_logs.push(JSON.stringify(args));
        console.error = (...args) => ps_buyer_error_logs.push(JSON.stringify(args));
    } else {
      console.log = console.warn = console.error = function() {};
    }
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_win_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_win_response.reportWinUrl = url;
        ps_report_win_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_win_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_win_response.interactionReportingUrls = eventUrlMap;
        ps_report_win_response.registerAdBeaconInvoked = true;
      }
      {
      reportWin = function(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals){
}
      }
      try{
      reportWin(auctionSignals, perBuyerSignals, signalsForWinner, buyerReportingSignals,
                              directFromSellerSignals, )
      } catch(ex){
        console.error(ex.message)
      }
      return {
        response: ps_report_win_response,
        buyerLogs: ps_buyer_logs,
        buyerErrors: ps_buyer_error_logs,
        buyerWarnings: ps_buyer_warning_logs
      }
    }

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd")
      console.error("Logging error from ScoreAd")
      console.warn("Logging warn from ScoreAd")
      return {
        desirability: score,
        allow_component_auction: false
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner+
          "&buyerAndSellerReportingId="+sellerReportingSignals.buyerAndSellerReportingId+
          "&selectedBuyerAndSellerReportingId="+sellerReportingSignals.selectedBuyerAndSellerReportingId)
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }
)JS_CODE";

constexpr absl::string_view kExpectedCodeWithReportWinDisabled = R"JS_CODE(
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

    //Handler method to call adTech provided reportResult method and wrap the
    // response with reportResult url and interaction reporting urls.
    function reportingEntryFunction(auctionConfig, sellerReportingSignals, directFromSellerSignals, enable_logging, buyerReportingMetadata, ) {
    ps_signalsForWinner = ""
    const ps_report_result_response = {
        reportResultUrl : "",
        interactionReportingUrls : {},
        sendReportToInvoked : false,
        registerAdBeaconInvoked : false,
      }
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
      globalThis.sendReportTo = function sendReportTo(url){
        if(ps_report_result_response.sendReportToInvoked) {
          throw new Error("sendReportTo function invoked more than once");
        }
        ps_report_result_response.reportResultUrl = url;
        ps_report_result_response.sendReportToInvoked = true;
      }
      globalThis.registerAdBeacon = function registerAdBeacon(eventUrlMap){
        if(ps_report_result_response.registerAdBeaconInvoked) {
          throw new Error("registerAdBeaconInvoked function invoked more than once");
        }
        ps_report_result_response.interactionReportingUrls=eventUrlMap;
        ps_report_result_response.registerAdBeaconInvoked = true;
      }
      ps_signalsForWinner = reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals);
      try{
      if(buyerReportingMetadata.enableReportWinUrlGeneration){
        var buyerOrigin = buyerReportingMetadata.buyerOrigin
        var functionSuffix = buyerOrigin.replace(/[^a-zA-Z0-9 ]/g, "")
        var auctionSignals = auctionConfig.auctionSignals
        var buyerReportingSignals = sellerReportingSignals
        delete buyerReportingSignals.desirability
        if(buyerReportingMetadata.hasOwnProperty("interestGroupName")){
          buyerReportingSignals.interestGroupName = buyerReportingMetadata.interestGroupName
        }
        buyerReportingSignals.madeHighestScoringOtherBid = buyerReportingMetadata.madeHighestScoringOtherBid
        buyerReportingSignals.joinCount = buyerReportingMetadata.joinCount
        buyerReportingSignals.recency = buyerReportingMetadata.recency
        buyerReportingSignals.modelingSignals = buyerReportingMetadata.modelingSignals
        perBuyerSignals = buyerReportingMetadata.perBuyerSignals
        buyerReportingSignals.seller = buyerReportingMetadata.seller
        buyerReportingSignals.adCost = buyerReportingMetadata.adCost
        buyerReportingSignals.dataVersion = buyerReportingMetadata.dataVersion
        if(buyerReportingMetadata.hasOwnProperty("buyerReportingId")){
          buyerReportingSignals.buyerReportingId = buyerReportingMetadata.buyerReportingId
        }
        // Absence of interest group indicates that this is a protected app
        // signals ad.
        if (buyerReportingMetadata.enableProtectedAppSignals &&
            (buyerReportingSignals.interestGroupName === null ||
             buyerReportingSignals.interestGroupName.trim() === "")) {
          functionSuffix += "ProtectedAppSignals";
        }
        var reportWinFunction = "reportWinWrapper"+functionSuffix+"(auctionSignals, perBuyerSignals, ps_signalsForWinner, buyerReportingSignals,"+
                              "directFromSellerSignals, enable_logging, )"
        var reportWinResponse = eval(reportWinFunction)
        return {
          reportResultResponse: ps_report_result_response,
          sellerLogs: ps_logs,
          sellerErrors: ps_errors,
          sellerWarnings: ps_warns,
          reportWinResponse: reportWinResponse.response,
          buyerLogs: reportWinResponse.buyerLogs,
          buyerErrors: reportWinResponse.buyerErrors,
          buyerWarnings: reportWinResponse.buyerWarnings,
      }
      }
      } catch(ex){
        console.error(ex.message)
      }
      return {
        reportResultResponse: ps_report_result_response,
        sellerLogs: ps_logs,
        sellerErrors: ps_errors,
        sellerWarnings: ps_warns,
      }
    }

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd")
      console.error("Logging error from ScoreAd")
      console.warn("Logging warn from ScoreAd")
      return {
        desirability: score,
        allow_component_auction: false
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner+
          "&buyerAndSellerReportingId="+sellerReportingSignals.buyerAndSellerReportingId+
          "&selectedBuyerAndSellerReportingId="+sellerReportingSignals.selectedBuyerAndSellerReportingId)
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }
)JS_CODE";

}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // FLEDGE_SERVICES_SELLER_CODE_WRAPPER_TEST_CONSTANTS_H_
