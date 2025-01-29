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

#ifndef SERVICES_COMMON_UTIL_AUCTION_SCOPE_UTIL_H_
#define SERVICES_COMMON_UTIL_AUCTION_SCOPE_UTIL_H_

#include <utility>

#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {
// Determines the scope of the auction (single seller, multiseller)
// based on SelectAdRequest.
AuctionScope GetAuctionScope(const SelectAdRequest& request);

// Determines the scope of the auction (single seller, multiseller)
// based on ScoreAdsRawRequest.
AuctionScope GetAuctionScope(
    const ScoreAdsRequest::ScoreAdsRawRequest& request);

// Determines if an auction is a component auction based on its scope.
inline bool IsComponentAuction(AuctionScope auction_scope) {
  return auction_scope ==
             AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER ||
         auction_scope ==
             AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER;
}
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_AUCTION_SCOPE_UTIL_H_
