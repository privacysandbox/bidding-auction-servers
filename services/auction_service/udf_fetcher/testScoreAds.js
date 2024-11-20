/**
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

function fibonacci(num) {
  if (num <= 1) return 1;
  return fibonacci(num - 1) + fibonacci(num - 2);
}

function scoreAd(ad_metadata, bid, auction_config, scoring_signals, bid_metadata, directFromSellerSignals) {
  // Do a random amount of work to generate the score:
  const score = fibonacci(Math.floor(Math.random() * 10 + 1));
  console.log('Logging from ScoreAd');
  console.error('Logging error from ScoreAd');
  console.warn('Logging warn from ScoreAd');
  return {
    desirability: score,
    allow_component_auction: false,
  };
}
function reportResult(auctionConfig, sellerReportingSignals, directFromSellerSignals) {
  console.log('Logging from ReportResult');
  if (sellerReportingSignals.topLevelSeller === undefined || sellerReportingSignals.topLevelSeller.length === 0) {
    sendReportTo(
      'http://test.com' +
        '&bid=' +
        sellerReportingSignals.bid +
        '&bidCurrency=' +
        sellerReportingSignals.bidCurrency +
        '&highestScoringOtherBid=' +
        sellerReportingSignals.highestScoringOtherBid +
        '&highestScoringOtherBidCurrency=' +
        sellerReportingSignals.highestScoringOtherBidCurrency +
        '&topWindowHostname=' +
        sellerReportingSignals.topWindowHostname +
        '&interestGroupOwner=' +
        sellerReportingSignals.interestGroupOwner
    );
  } else {
    sendReportTo(
      'http://test.com&topLevelSeller=' +
        sellerReportingSignals.topLevelSeller +
        '&componentSeller=' +
        sellerReportingSignals.componentSeller
    );
  }
  registerAdBeacon({ clickEvent: 'http://click.com' });
  return { testSignal: 'testValue' };
}
