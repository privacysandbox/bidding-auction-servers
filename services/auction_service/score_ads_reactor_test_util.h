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

#ifndef SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_
#define SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

using ProtectedAppSignalsAdWithBidMetadata =
    ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata;

constexpr float kTestBid = 1.25;
constexpr char kTestProtectedAppSignalsAdOwner[] = "https://PAS-Ad-Owner.com";

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid = kTestBid);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_SCORE_ADS_REACTOR_TEST_UTIL_H_
