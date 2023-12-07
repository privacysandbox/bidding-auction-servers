// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/auction_service/score_ads_reactor_test_util.h"

#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {

ProtectedAppSignalsAdWithBidMetadata GetProtectedAppSignalsAdWithBidMetadata(
    absl::string_view render_url, float bid) {
  ProtectedAppSignalsAdWithBidMetadata ad;
  ad.mutable_ad()->mutable_struct_value()->MergeFrom(
      MakeAnAd(render_url, "arbitraryMetadataKey", 2));
  ad.set_render(render_url);
  ad.set_bid(bid);
  ad.set_owner(kTestProtectedAppSignalsAdOwner);
  return ad;
}

}  // namespace privacy_sandbox::bidding_auction_servers
