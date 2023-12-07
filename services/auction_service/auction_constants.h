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

namespace privacy_sandbox::bidding_auction_servers {

// See ScoreAdInput for more detail on each field.
enum class ScoreAdArgs : int {
  kAdMetadata = 0,
  kBid,
  kAuctionConfig,
  kScoringSignals,
  kDeviceSignals,
  // This is only added to prevent errors in the score ad script, and
  // will always be an empty object.
  kDirectFromSellerSignals,
  kFeatureFlags,
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_AUCTION_CONSTANTS_H_
