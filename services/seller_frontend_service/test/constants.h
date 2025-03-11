/*
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_TEST_CONSTANTS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_TEST_CONSTANTS_H_

#include <array>
#include <cstdint>

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kSamplePublisher[] = "foo_publisher";
inline constexpr char kSampleIgName[] = "foo_ig_name";
inline constexpr char kSampleBiddingSignalKey1[] = "bidding_signal_key_1";
inline constexpr char kSampleBiddingSignalKey2[] = "bidding_signal_key_2";
inline constexpr char kSampleAdRenderId1[] = "ad_render_id_1";
inline constexpr char kSampleAdRenderId2[] = "ad_render_id_2";
inline constexpr char kSampleAdComponentRenderId1[] =
    "ad_component_render_id_1";
inline constexpr char kSampleAdComponentRenderId2[] =
    "ad_component_render_id_2";
inline constexpr char kSampleUserBiddingSignals[] =
    R"(["this should be a", "JSON array", "that gets parsed", "into separate list values"])";
inline constexpr int kSampleJoinCount = 1;
inline constexpr int kSampleBidCount = 2;
inline constexpr int kSampleRecency = 3;
inline constexpr int kSampleRecencyMs = 3000;
inline constexpr float kSampleBid = 1.75;
inline constexpr float kSampleScore = 1.12;
inline constexpr char kSampleIgOwner[] = "foo_owner";
inline constexpr char kSampleIgOrigin[] = "ig_origin";
inline constexpr char kSampleGenerationId[] =
    "6fa459ea-ee8a-3ca4-894e-db77e160355e";
inline constexpr int kSampleRequestMs = 99999;
inline constexpr bool kSampleEnforceKAnon = true;
inline constexpr char kSampleErrorMessage[] = "BadError";
inline constexpr int32_t kSampleErrorCode = 400;
inline constexpr char kTestEvent1[] = "click";
inline constexpr char kTestInteractionUrl1[] = "http://click.com";
inline constexpr char kTestEvent2[] = "scroll";
inline constexpr char kTestInteractionUrl2[] = "http://scroll.com";
inline constexpr char kTestEvent3[] = "close";
inline constexpr char kTestInteractionUrl3[] = "http://close.com";
inline constexpr char kTestReportResultUrl[] = "http://reportResult.com";
inline constexpr char kTestReportWinUrl[] = "http://reportWin.com";
inline constexpr char kConsentedDebugToken[] = "xyz";
inline constexpr char kUsdIso[] = "USD";
inline constexpr char kTestBuyerReportingId[] = "testBuyerReportingId";
inline constexpr char kSampleAdRenderUrl[] = "adRenderUrl";
inline constexpr char kSampleAdComponentRenderUrl[] = "adComponentRenderUrl";
inline constexpr float kSampleModifiedBid = 1.23;
inline constexpr char kSampleBidCurrency[] = "bidCurrency";
inline constexpr char kSampleAdMetadata[] = "adMetadata";
inline constexpr char kSampleAdAuctionResultNonce[] = "ad-auction-result-n0nce";

inline constexpr std::array<uint8_t, 5> kSampleAdRenderUrlHash = {1, 2, 3, 4,
                                                                  5};

inline constexpr std::array<uint8_t, 5> kSampleAdComponentRenderUrlsHash = {
    5, 4, 3, 2, 1};

inline constexpr std::array<uint8_t, 5> kSampleReportingIdHash = {2, 4, 6, 8,
                                                                  9};

inline constexpr char kSampleBuyerAndSellerReportingId[] =
    "buyerAndSellerReportingId";
inline constexpr char kSampleBuyerReportingId[] = "buyerReportingId";
inline constexpr char kSampleSelectedBuyerAndSellerReportingId[] =
    "selectedBuyerAndSellerReportingId";
inline constexpr int kSampleWinnerPositionalIndex = 5;
inline constexpr int kSampleIgIndex = 10;
inline constexpr std::array<uint8_t, 5> kSampleBucket = {1, 3, 5, 7, 9};
inline constexpr int kSampleValue = 21;

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_TEST_CONSTANTS_H_
