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

#include "services/seller_frontend_service/providers/http_scoring_signals_async_provider.h"

#include <string>
#include <utility>

namespace privacy_sandbox::bidding_auction_servers {

HttpScoringSignalsAsyncProvider::HttpScoringSignalsAsyncProvider(
    std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
        http_seller_kv_async_client)
    : http_seller_kv_async_client_(std::move(http_seller_kv_async_client)) {}

void HttpScoringSignalsAsyncProvider::Get(
    const ScoringSignalsRequest& scoring_signals_request,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<ScoringSignals>>) &&>
        on_done,
    absl::Duration timeout) const {
  auto request = std::make_unique<GetSellerValuesInput>();
  for (const auto& buyer_get_bid_response_pair :
       scoring_signals_request.buyer_bids_map_) {
    for (const auto& ad : buyer_get_bid_response_pair.second->bids()) {
      request->render_urls.emplace_back(ad.render());
      request->ad_component_render_urls.insert(
          request->ad_component_render_urls.end(), ad.ad_components().begin(),
          ad.ad_components().end());
    }
  }
  request->client_type = scoring_signals_request.client_type_;
  http_seller_kv_async_client_->Execute(
      std::move(request), scoring_signals_request.filtering_metadata_,
      [on_done = std::move(on_done)](
          absl::StatusOr<std::unique_ptr<GetSellerValuesOutput>>
              kv_output) mutable {
        absl::StatusOr<std::unique_ptr<ScoringSignals>> res;
        if (kv_output.ok()) {
          res = std::make_unique<ScoringSignals>();
          res.value()->scoring_signals =
              std::make_unique<std::string>(kv_output.value()->result);
        } else {
          res = kv_output.status();
        }
        std::move(on_done)(std::move(res));
      },
      timeout);
}

}  // namespace privacy_sandbox::bidding_auction_servers
