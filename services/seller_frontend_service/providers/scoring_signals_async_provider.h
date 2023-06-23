// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SERVICES_SFE_PROVIDERS_SCORING_SIGNALS_ASYNC_PROVIDER_H_
#define SERVICES_SFE_PROVIDERS_SCORING_SIGNALS_ASYNC_PROVIDER_H_

#include <memory>

#include "services/common/providers/async_provider.h"
#include "services/seller_frontend_service/data/scoring_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

// The classes implementing this interface provide the external signals
// required by SellerFrontEnd Service for the bidding process.
// Different implementations of this interface can use different external
// sources such as an externally sharded gRPC Key Value server, etc.
using ScoringSignalsAsyncProvider =
    AsyncProvider<BuyerBidsList, ScoringSignals>;

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SFE_PROVIDERS_SCORING_SIGNALS_ASYNC_PROVIDER_H_
