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
#ifndef SERVICES_AUCTION_SERVICE_SELLER_UDF_WRAPPER_TEST_CONSTANTS_H_
#define SERVICES_AUCTION_SERVICE_SELLER_UDF_WRAPPER_TEST_CONSTANTS_H_

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr absl::string_view kSellerBaseCode = R"JS_CODE(
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

constexpr absl::string_view kSellerBaseCodeWithBadReportResult = R"JS_CODE(
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
        someUnknownFunction();
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+"&bid="+sellerReportingSignals.bid+"&bidCurrency="+sellerReportingSignals.bidCurrency+"&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+"&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+"&topWindowHostname="+sellerReportingSignals.topWindowHostname+"&interestGroupOwner="+sellerReportingSignals.interestGroupOwner)
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }
)JS_CODE";

constexpr absl::string_view kSellerBaseCodeWithPrivateAggregationNumerical =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd");
      console.error("Logging error from ScoreAd");
      console.warn("Logging warn from ScoreAd");
              if(globalThis.privateAggregation){
          const contribution = {
            bucket: 1512366075204171022513085661201620700n,
            value: 10,
          };
          globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
          globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.loss', contribution);
          globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.always', contribution);
          globalThis.privateAggregation.contributeToHistogramOnEvent('click', contribution);
        }
      return {
        desirability: score,
        allow_component_auction: false
      };
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
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner);
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller);
        }
        registerAdBeacon({"clickEvent":"http://click.com"});
        return {"testSignal":"testValue"};
    }
)JS_CODE";

constexpr absl::string_view
    kSellerBaseCodeWithPrivateAggregationNumericalInReportResult =
        R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd");
      console.error("Logging error from ScoreAd");
      console.warn("Logging warn from ScoreAd");
      if (globalThis.privateAggregation) {
        const contribution = {
          bucket: 1512366075204171022513085661201620700n,
          value: 10,
        };
        globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
        globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.loss', contribution);
        globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.always', contribution);
        globalThis.privateAggregation.contributeToHistogramOnEvent('click', contribution);
      }
      return {
        desirability: score,
        allow_component_auction: false
      };
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if (globalThis.privateAggregation) {
          const contribution = {
            bucket: 1512366075204171022513085661201620700n,
            value: 10,
          };
          globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.win', contribution);
          globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.loss', contribution);
          globalThis.privateAggregation.contributeToHistogramOnEvent('reserved.always', contribution);
          globalThis.privateAggregation.contributeToHistogramOnEvent('click', contribution);
        }
        if (sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0) {
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner);
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller);
        }
        registerAdBeacon({"clickEvent":"http://click.com"});
        return {"testSignal":"testValue"};
    }
)JS_CODE";

constexpr absl::string_view kSellerBaseCodeWithPrivateAggregationSignalObjects =
    R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd");
      console.error("Logging error from ScoreAd");
      console.warn("Logging warn from ScoreAd");
      const signal_object_contribution = {
        bucket: {baseValue: "winning-bid", scale: 1.2, offset: 100},
        value: {baseValue: "winning-bid", scale: 1.0, offset: 0},
      };
      privateAggregation.contributeToHistogramOnEvent('reserved.win', signal_object_contribution);

      const numerical_contribution = {
        bucket: 100,
        value: 200,
      };
      privateAggregation.contributeToHistogramOnEvent('reserved.win', numerical_contribution);

      return {
        desirability: score,
        allow_component_auction: false
      };
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+"&bid="+sellerReportingSignals.bid+"&bidCurrency="+sellerReportingSignals.bidCurrency+"&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+"&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+"&topWindowHostname="+sellerReportingSignals.topWindowHostname+"&interestGroupOwner="+sellerReportingSignals.interestGroupOwner);
        } else {
          sendReportTo("http://test.com&topLevelSeller="+sellerReportingSignals.topLevelSeller+"&componentSeller="+sellerReportingSignals.componentSeller);
        }
        registerAdBeacon({"clickEvent":"http://click.com"});
        return {"testSignal":"testValue"};
    }
)JS_CODE";

constexpr absl::string_view kSellerBaseCodeForComponentAuction = R"JS_CODE(
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
        ad: "adMetadata",
        desirability: score,
        bid: 2.0,
        bidCurrency: "EUR",
        incomingBidInSellerCurrency: "EUR",
        allowComponentAuction: true
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        if(sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0){
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner)
        } else {
          sendReportTo("http://test.com"+
          "&bid="+sellerReportingSignals.bid+
          "&bidCurrency="+sellerReportingSignals.bidCurrency+
          "&dataVersion="+sellerReportingSignals.dataVersion+
          "&highestScoringOtherBid="+sellerReportingSignals.highestScoringOtherBid+
          "&highestScoringOtherBidCurrency="+sellerReportingSignals.highestScoringOtherBidCurrency+
          "&topWindowHostname="+sellerReportingSignals.topWindowHostname+
          "&interestGroupOwner="+sellerReportingSignals.interestGroupOwner+
          "&topLevelSeller="+sellerReportingSignals.topLevelSeller+
          "&modifiedBid="+sellerReportingSignals.modifiedBid)
        }
        registerAdBeacon({"clickEvent":"http://click.com"})
        return {"testSignal":"testValue"}
    }
)JS_CODE";

constexpr absl::string_view kSellerBaseCodeWithNoSignalsForWinner = R"JS_CODE(
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
    }
)JS_CODE";

constexpr absl::string_view kExpectedSellerCodeWithScoreAdAndReportResult =
    R"JS_CODE(
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

constexpr absl::string_view kExpectedCodeWithReportingDisabled = R"JS_CODE(
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
#endif  // SERVICES_AUCTION_SERVICE_SELLER_UDF_WRAPPER_TEST_CONSTANTS_H_
