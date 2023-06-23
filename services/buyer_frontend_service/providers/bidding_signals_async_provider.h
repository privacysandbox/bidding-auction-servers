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

#ifndef SERVICES_BFE_SERVICE_PROVIDERS_BIDDING_SIGNALS_ASYNC_CLIENT_H_
#define SERVICES_BFE_SERVICE_PROVIDERS_BIDDING_SIGNALS_ASYNC_CLIENT_H_

#include <string>

#include "services/buyer_frontend_service/data/bidding_signals.h"
#include "services/common/providers/async_provider.h"

namespace privacy_sandbox::bidding_auction_servers {

// TODO(b/277982039): Simplify and remove Async Provider interface
struct BiddingSignalsRequest {
  explicit BiddingSignalsRequest(
      const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request,
      const absl::flat_hash_map<std::string, std::string>& filtering_metadata)
      : get_bids_raw_request_(get_bids_raw_request),
        filtering_metadata_(filtering_metadata) {}

  // The objects here should be owned by the caller of the provider class
  // using this struct as a parameter. They're only required in the context of
  // the function call, and can ideally be reduced to const& parameters to the
  // method.
  const GetBidsRequest::GetBidsRawRequest& get_bids_raw_request_;
  const absl::flat_hash_map<std::string, std::string>& filtering_metadata_;
};

// The classes implementing this interface provide the external signals
// required by BuyerFrontEnd Service for the bidding process.
// Different implementations of this interface can use different external
// sources such as an externally sharded gRPC Key Value server, etc.
using BiddingSignalsAsyncProvider =
    AsyncProvider<BiddingSignalsRequest, BiddingSignals>;

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BFE_SERVICE_PROVIDERS_BIDDING_SIGNALS_ASYNC_CLIENT_H_
