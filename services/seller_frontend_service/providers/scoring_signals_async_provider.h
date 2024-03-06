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
#include <string>
#include <utility>

#include "services/common/providers/async_provider.h"
#include "services/seller_frontend_service/data/scoring_signals.h"

namespace privacy_sandbox::bidding_auction_servers {

struct ScoringSignalsRequest {
  explicit ScoringSignalsRequest(
      const BuyerBidsResponseMap& buyer_bids_map,
      const absl::flat_hash_map<std::string, std::string>& filtering_metadata,
      ClientType client_type, std::string seller_kv_experiment_group_id = "")
      : client_type_(client_type),
        buyer_bids_map_(buyer_bids_map),
        filtering_metadata_(filtering_metadata),
        seller_kv_experiment_group_id_(
            std::move(seller_kv_experiment_group_id)) {}

  ClientType client_type_;
  // The objects here should be owned by the caller of the provider class
  // using this struct as a parameter. They're only required in the context of
  // the function call, and can ideally be reduced to const& parameters to the
  // method.
  const BuyerBidsResponseMap& buyer_bids_map_;
  const absl::flat_hash_map<std::string, std::string>& filtering_metadata_;

  // [DSP] Optional ID for experiments conducted by buyer. By spec, valid values
  // are [0, 65535].
  std::string seller_kv_experiment_group_id_;
};

// The classes implementing this interface provide the external signals
// required by SellerFrontEnd Service for the bidding process.
// Different implementations of this interface can use different external
// sources such as an externally sharded gRPC Key Value server, etc.
using ScoringSignalsAsyncProvider =
    AsyncProvider<ScoringSignalsRequest, ScoringSignals>;

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SFE_PROVIDERS_SCORING_SIGNALS_ASYNC_PROVIDER_H_
