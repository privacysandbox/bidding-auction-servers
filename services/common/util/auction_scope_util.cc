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

#include "services/common/util/auction_scope_util.h"

namespace privacy_sandbox::bidding_auction_servers {
AuctionScope GetAuctionScope(const SelectAdRequest& request) {
  if (!request.auction_config().top_level_seller().empty()) {
    switch (request.auction_config().top_level_cloud_platform()) {
      case EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_GCP:
      case EncryptionCloudPlatform::ENCRYPTION_CLOUD_PLATFORM_AWS:
        // If there is a top level seller and a cloud platform for the
        // top level seller it's a server component auciton.
        return AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER;
      default:
        // If no cloud platform is specified for the top level seller
        // we assume that it's a device component auction.
        return AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER;
    }
  }
  // If there is no "top level seller" specified and "component auction
  // results" are included in the requet, we assume that it's a top level
  // auction.
  if (request.component_auction_results_size() > 0) {
    return AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER;
  }
  return AuctionScope::AUCTION_SCOPE_SINGLE_SELLER;
}

AuctionScope GetAuctionScope(
    const ScoreAdsRequest::ScoreAdsRawRequest& request) {
  // Component Auction
  if (!request.top_level_seller().empty()) {
    // Assume that it's a device component auction, since the functionality
    // for Auction Server is same for device and server component auctions.
    return AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER;
  }
  // If there is no top level seller specified and other auction
  // results included in the request, we assume that it's a top level auction.
  if (request.component_auction_results_size() > 0) {
    return AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER;
  }
  return AuctionScope::AUCTION_SCOPE_SINGLE_SELLER;
}
}  // namespace privacy_sandbox::bidding_auction_servers
