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

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kBuyer1Origin[] = "http://buyer1.com";
constexpr absl::string_view kSellerBaseCode = R"JS_CODE(
    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, device_signals, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd");
      return {
        desirability: score,
        allow_component_auction: false
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        sendReportTo("http://test.com")
        var beacons = new Map();
        beacons.set("clickEvent","http://click.com")
        registerAdBeacon(beacons)
        return "testSignalsForWinner"
    }
)JS_CODE";

constexpr absl::string_view kExpectedFinalCode = R"JS_CODE(
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

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, device_signals, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd");
      return {
        desirability: score,
        allow_component_auction: false
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        sendReportTo("http://test.com")
        var beacons = new Map();
        beacons.set("clickEvent","http://click.com")
        registerAdBeacon(beacons)
        return "testSignalsForWinner"
    }

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
)JS_CODE";

constexpr absl::string_view kExpectedCodeWithReportingDisabled = R"JS_CODE(
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

    function fibonacci(num) {
      if (num <= 1) return 1;
      return fibonacci(num - 1) + fibonacci(num - 2);
    }

    function scoreAd(ad_metadata, bid, auction_config, scoring_signals, device_signals, directFromSellerSignals){
      // Do a random amount of work to generate the score:
      const score = fibonacci(Math.floor(Math.random() * 10 + 1));
      console.log("Logging from ScoreAd");
      return {
        desirability: score,
        allow_component_auction: false
      }
    }
    function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals){
        console.log("Logging from ReportResult");
        sendReportTo("http://test.com")
        var beacons = new Map();
        beacons.set("clickEvent","http://click.com")
        registerAdBeacon(beacons)
        return "testSignalsForWinner"
    }
)JS_CODE";
}  // namespace privacy_sandbox::bidding_auction_servers
#endif  // FLEDGE_SERVICES_SELLER_CODE_WRAPPER_TEST_CONSTANTS_H_
