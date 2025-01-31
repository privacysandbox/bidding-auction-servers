/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_AUCTION_SERVICE_AUCTION_CONSTANTS_H_
#define SERVICES_AUCTION_SERVICE_AUCTION_CONSTANTS_H_

#include <cstdint>

namespace privacy_sandbox::bidding_auction_servers {

constexpr char DispatchHandlerFunctionWithSellerWrapper[] =
    "scoreAdEntryFunction";
constexpr char kNoTrustedScoringSignals[] = "Empty trusted scoring signals";
constexpr char kAdComponentRenderUrlsProperty[] = "adComponentRenderUrls";
constexpr char kRenderUrlsPropertyForKVResponse[] = "renderUrls";

// The following fields are expected to returned by ScoreAd response
constexpr char kRenderUrlsPropertyForScoreAd[] = "renderUrl";
constexpr char kTopLevelSellerFieldPropertyForScoreAd[] = "topLevelSeller";
constexpr char kComponentSellerFieldPropertyForScoreAd[] = "componentSeller";
constexpr char kAuctionDebugLossUrlPropertyForScoreAd[] = "auctionDebugLossUrl";
constexpr char kAuctionDebugWinUrlPropertyForScoreAd[] = "auctionDebugWinUrl";
constexpr char kRejectReasonPropertyForScoreAd[] = "rejectReason";
constexpr char kDesirabilityPropertyForScoreAd[] = "desirability";
constexpr char kAllowComponentAuctionPropertyForScoreAd[] =
    "allowComponentAuction";
constexpr char kBidCurrencyPropertyForScoreAd[] = "bidCurrency";
constexpr char kSellerDataVersionPropertyForScoreAd[] = "dataVersion";
constexpr char kAdMetadataForComponentAuction[] = "ad";
constexpr char kModifiedBidForComponentAuction[] = "bid";
constexpr char kIncomingBidInSellerCurrency[] = "incomingBidInSellerCurrency";
constexpr char kDebugReportUrlsPropertyForScoreAd[] = "debugReportUrls";
constexpr char kScoreAdBlobVersion[] = "v1";
constexpr char kIGOwnerPropertyForScoreAd[] = "interestGroupOwner";
constexpr char kTopWindowHostnamePropertyForScoreAd[] = "topWindowHostname";
constexpr char kBuyerReportingIdForScoreAd[] = "buyerReportingId";
constexpr char kBuyerAndSellerReportingIdForScoreAd[] =
    "buyerAndSellerReportingId";
constexpr char kSelectedBuyerAndSellerReportingIdForScoreAd[] =
    "selectedBuyerAndSellerReportingId";

// TODO(b/306257710): Update to differentiate from kScoreAdBlobVersion.
constexpr char kReportingBlobVersion[] = "v1";

constexpr int kArgSizeWithWrapper = 7;
// Acceptable error margin for float currency comparisons.
// Arbitrarily specified to be one one-hundred-thousandth,
// because this is thousandths-of-a-cent,
// which seems like an irrelevantly small amount for US Dollars.
constexpr float kCurrencyFloatComparisonEpsilon = 0.00001f;

// k-anon status enums used to populate kAnonStatus field in browserSignals of
// reportWin().
constexpr char kPassedAndEnforcedKAnonStatus[] = "passedAndEnforced";
constexpr char kNotCalculatedKAnonStatus[] = "notCalculated";

// See ScoreAdInput for more detail on each field.
enum class ScoreAdArgs : std::uint8_t {
  kAdMetadata = 0,
  kBid,
  kAuctionConfig,
  kScoringSignals,
  kBidMetadata,
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  kDirectFromSellerSignals,
  kFeatureFlags,
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_AUCTION_CONSTANTS_H_
