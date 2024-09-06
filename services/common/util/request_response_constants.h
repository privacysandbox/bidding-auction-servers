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

#ifndef SERVICES_COMMON_UTIL_REQUEST_RESPONSE_CONSTANTS_H_
#define SERVICES_COMMON_UTIL_REQUEST_RESPONSE_CONSTANTS_H_

#include <stddef.h>

#include <array>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

// Maximum number of keys in the incoming ProtectedAudienceInput request.
inline constexpr int kNumRequestRootKeys = 6;

// Maximum number of keys that will be populated in the encoded CBOR
// ConsentedDebugConfig request.
inline constexpr int kNumConsentedDebugConfigKeys = 3;

// Maximum number of keys that will be populated in the encoded CBOR
// AuctionResult response.
inline constexpr int kNumAuctionResultKeys = 14;

// Maximum number of keys that will be populated in the encoded CBOR
// WinReportingUrls response.
inline constexpr int kNumWinReportingUrlsKeys = 3;

// Maximum number of keys that will be populated in the encoded CBOR
// ReportingUrls response.
inline constexpr int kNumReportingUrlsKeys = 2;

// Minimum size of the returned response in bytes.
inline constexpr size_t kMinAuctionResultBytes = 512;

// Constants for the fields in the request payload.
inline constexpr char kVersion[] = "version";
inline constexpr char kPublisher[] = "publisher";
inline constexpr char kGenerationId[] = "generationId";
inline constexpr char kAdtechDebugId[] = "adtechDebugId";
inline constexpr char kSellerDebugId[] = "sellerDebugId";
inline constexpr char kBuyerDebugId[] = "buyerDebugId";
inline constexpr char kDebugReporting[] = "enableDebugReporting";
inline constexpr char kInterestGroups[] = "interestGroups";
inline constexpr char kAdRenderId[] = "ad_render_id";
inline constexpr char kName[] = "name";
inline constexpr char kBiddingSignalsKeys[] = "biddingSignalsKeys";
inline constexpr char kUserBiddingSignals[] = "userBiddingSignals";
inline constexpr char kAds[] = "ads";
inline constexpr char kAdComponents[] = "components";
inline constexpr char kBrowserSignals[] = "browserSignals";
inline constexpr char kBidCount[] = "bidCount";
inline constexpr char kJoinCount[] = "joinCount";
inline constexpr char kRecency[] = "recency";
inline constexpr char kRecencyMs[] = "recencyMs";
inline constexpr char kPrevWins[] = "prevWins";
inline constexpr char kConsentedDebugConfig[] = "consentedDebugConfig";
inline constexpr char kIsConsented[] = "isConsented";
inline constexpr char kToken[] = "token";
inline constexpr char kIsDebugResponse[] = "isDebugInfoInResponse";
inline constexpr int kRelativeTimeIndex = 0;
inline constexpr int kAdRenderIdIndex = 1;

// Constants for the fields in response sent back to clients.
inline constexpr char kBid[] = "bid";                              // length: 3
inline constexpr char kScore[] = "score";                          // length: 5
inline constexpr char kChaff[] = "isChaff";                        // length: 7
inline constexpr char kAdMetadata[] = "adMetadata";                // length: 10
inline constexpr char kAdRenderUrl[] = "adRenderURL";              // length: 11
inline constexpr char kBidCurrency[] = "bidCurrency";              // length: 11
inline constexpr char kBiddingGroups[] = "biddingGroups";          // length: 13
inline constexpr char kTopLevelSeller[] = "topLevelSeller";        // length: 14
inline constexpr char kBuyerReportingId[] = "buyerReportingId";    // length: 16
inline constexpr char kWinReportingUrls[] = "winReportingURLs";    // length: 16
inline constexpr char kInterestGroupName[] = "interestGroupName";  // length: 17
inline constexpr char kInterestGroupOwner[] =
    "interestGroupOwner";  // length: 18

inline constexpr std::array<absl::string_view, kNumRequestRootKeys>
    kRequestRootKeys = {
        kVersion,      kPublisher,      kInterestGroups,
        kGenerationId, kDebugReporting, kConsentedDebugConfig,
};
inline constexpr int kNumInterestGroupKeys = 6;
inline constexpr std::array<absl::string_view, kNumInterestGroupKeys>
    kInterestGroupKeys = {kName, kBiddingSignalsKeys, kUserBiddingSignals,
                          kAds,  kAdComponents,       kBrowserSignals};

inline constexpr int kNumBrowserSignalKeys = 5;
inline constexpr std::array<absl::string_view, kNumBrowserSignalKeys>
    kBrowserSignalKeys = {kBidCount, kJoinCount, kRecency, kPrevWins,
                          kRecencyMs};
inline constexpr std::array<absl::string_view, kNumConsentedDebugConfigKeys>
    kConsentedDebugConfigKeys = {
        kIsConsented,
        kToken,
        kIsDebugResponse,
};

inline constexpr char kTimeoutMs[] = "TimeoutMs";

inline constexpr int kNumErrorKeys = 2;
inline constexpr char kError[] = "error";
inline constexpr char kMessage[] = "message";
inline constexpr char kCode[] = "code";
inline constexpr char kBuyerReportingUrls[] = "buyerReportingURLs";
inline constexpr char kComponentSellerReportingUrls[] =
    "componentSellerReportingURLs";
inline constexpr char kTopLevelSellerReportingUrls[] =
    "topLevelSellerReportingURLs";
inline constexpr char kReportingUrl[] = "reportingURL";
inline constexpr char kInteractionReportingUrls[] = "interactionReportingURLs";

inline constexpr std::array<absl::string_view, kNumErrorKeys> kErrorKeys = {
    kMessage,
    kCode,
};
inline constexpr std::array<absl::string_view, kNumWinReportingUrlsKeys>
    kWinReportingKeys = {kBuyerReportingUrls, kComponentSellerReportingUrls,
                         kTopLevelSellerReportingUrls};
inline constexpr std::array<absl::string_view, kNumReportingUrlsKeys>
    kReportingKeys = {kReportingUrl, kInteractionReportingUrls};

enum class AuctionType : int { kProtectedAudience, kProtectedAppSignals };

// log verbosity

inline constexpr int kPlain = 1;  // plaintext B&A request and response served
inline constexpr int kNoisyWarn =
    2;  // non-critical error, use PS_LOG(ERROR, *) for critical error
inline constexpr int kUdfLog = 3;
inline constexpr int kSuccess = 3;
inline constexpr int kNoisyInfo = 4;
inline constexpr int kDispatch = 4;  // UDF dispatch request and response
inline constexpr int kOriginated =
    5;  // plaintext B&A request and response originated from server
inline constexpr int kKVLog = 5;  // KV request response
inline constexpr int kStats = 5;  // Stats log like time , byte size, etc.
inline constexpr int kEncrypted = 6;
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_REQUEST_RESPONSE_CONSTANTS_H_
