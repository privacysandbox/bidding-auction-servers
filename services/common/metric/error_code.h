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

#ifndef SERVICES_COMMON_METRIC_ERROR_CODE_H_
#define SERVICES_COMMON_METRIC_ERROR_CODE_H_

#include "absl/strings/string_view.h"

// Defines error codes for error count instrumentation per bidding & auction
// server.
namespace privacy_sandbox::bidding_auction_servers::metric {

inline constexpr absl::string_view kAuctionScoreAdsDispatchResponseError =
    "ScoreAds dispatch response error";
inline constexpr absl::string_view kAuctionScoreAdsFailedToDispatchCode =
    "ScoreAds failed to dispatch code";
inline constexpr absl::string_view
    kAuctionScoreAdsFailedToInsertDispatchRequest =
        "ScoreAds failed to create dispatch code";
inline constexpr absl::string_view kAuctionScoreAdsNoAdSelected =
    "ScoreAds no ad selected";

inline constexpr absl::string_view kAuctionErrorCode[]{
    kAuctionScoreAdsDispatchResponseError,
    kAuctionScoreAdsFailedToDispatchCode,
    kAuctionScoreAdsNoAdSelected,
};

inline constexpr absl::string_view kBfeBiddingSignalsResponseError =
    "Bidding signals response error";
inline constexpr absl::string_view kBfeGenerateBidsFailedToCall =
    "GenerateBids failed to call";
inline constexpr absl::string_view kBfeGenerateBidsResponseError =
    "GenerateBids response error";
inline constexpr absl::string_view
    kBfeGenerateProtectedAppSignalsBidsFailedToCall =
        "GenerateProtectedAppSignalsBids failed to call";
inline constexpr absl::string_view
    kBfeGenerateProtectedAppSignalsBidsResponseError =
        "GenerateProtectedAppSignalsBids response error";

inline constexpr absl::string_view kBfeErrorCode[]{
    kBfeBiddingSignalsResponseError,
    kBfeGenerateBidsFailedToCall,
    kBfeGenerateBidsResponseError,
    kBfeGenerateProtectedAppSignalsBidsFailedToCall,
    kBfeGenerateProtectedAppSignalsBidsResponseError,
};

inline constexpr absl::string_view kBiddingGenerateBidsDispatchResponseError =
    "GenerateBids dispatch response error";
inline constexpr absl::string_view kBiddingGenerateBidsTimedOutError =
    "GenerateBids dispatch timed out";
inline constexpr absl::string_view kBiddingGenerateBidsFailedToDispatchCode =
    "GenerateBids failed to dispatch code";

inline constexpr absl::string_view kBiddingErrorCode[]{
    kBiddingGenerateBidsDispatchResponseError,
    kBiddingGenerateBidsTimedOutError,
    kBiddingGenerateBidsFailedToDispatchCode,
};

inline constexpr absl::string_view kSfeGetBidsFailedToCall =
    "GetBids failed to call";
inline constexpr absl::string_view kSfeGetBidsResponseError =
    "GetBids response error";
inline constexpr absl::string_view kSfeScoreAdsFailedToCall =
    "ScoreAds failed to call";
inline constexpr absl::string_view kSfeScoreAdsResponseError =
    "ScoreAds response error";
inline constexpr absl::string_view kSfeScoringSignalsResponseError =
    "Scoring signals response error";
inline constexpr absl::string_view kSfeSelectAdNoSuccessfulBid =
    "SelectAd no successful bid";
inline constexpr absl::string_view kSfeSelectAdRequestBadInput =
    "SelectAd request bad input";

inline constexpr absl::string_view kSfeErrorCode[]{
    kSfeGetBidsFailedToCall,         kSfeGetBidsResponseError,
    kSfeScoreAdsFailedToCall,        kSfeScoreAdsResponseError,
    kSfeScoringSignalsResponseError, kSfeSelectAdNoSuccessfulBid,
    kSfeSelectAdRequestBadInput,
};

}  // namespace privacy_sandbox::bidding_auction_servers::metric

#endif  // SERVICES_COMMON_METRIC_ERROR_CODE_H_
