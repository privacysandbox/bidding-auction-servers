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

#include "services/buyer_frontend_service/providers/http_bidding_signals_async_provider.h"

namespace privacy_sandbox::bidding_auction_servers {

HttpBiddingSignalsAsyncProvider::HttpBiddingSignalsAsyncProvider(
    std::unique_ptr<AsyncClient<GetBuyerValuesInput, GetBuyerValuesOutput>>
        http_buyer_kv_async_client)
    : http_buyer_kv_async_client_(std::move(http_buyer_kv_async_client)) {}

void HttpBiddingSignalsAsyncProvider::Get(
    const BiddingSignalsRequest& bidding_signals_request,
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<BiddingSignals>>,
                            GetByteSize) &&>
        on_done,
    absl::Duration timeout) const {
  auto request = std::make_unique<GetBuyerValuesInput>();
  request->hostname =
      bidding_signals_request.get_bids_raw_request_.publisher_name();
  request->client_type =
      bidding_signals_request.get_bids_raw_request_.client_type();
  if (bidding_signals_request.get_bids_raw_request_
          .has_buyer_kv_experiment_group_id()) {
    request->buyer_kv_experiment_group_id =
        absl::StrCat(bidding_signals_request.get_bids_raw_request_
                         .buyer_kv_experiment_group_id());
  }
  absl::StatusOr<std::unique_ptr<BiddingSignals>> output =
      std::make_unique<BiddingSignals>();

  for (const auto& interest_group :
       bidding_signals_request.get_bids_raw_request_.buyer_input()
           .interest_groups()) {
    request->interest_group_names.emplace(interest_group.name());
    request->keys.insert(interest_group.bidding_signals_keys().begin(),
                         interest_group.bidding_signals_keys().end());
  }

  auto status = http_buyer_kv_async_client_->Execute(
      std::move(request), bidding_signals_request.filtering_metadata_,
      [res = std::move(output), on_done = std::move(on_done)](
          absl::StatusOr<std::unique_ptr<GetBuyerValuesOutput>>
              buyer_kv_output) mutable {
        GetByteSize get_byte_size;
        if (buyer_kv_output.ok()) {
          // TODO(b/258281777): Add ads and buyer signals from KV.
          res.value()->trusted_signals =
              std::make_unique<std::string>(buyer_kv_output.value()->result);
          get_byte_size.request = buyer_kv_output.value()->request_size;
          get_byte_size.response = buyer_kv_output.value()->response_size;
        } else {
          res = buyer_kv_output.status();
        }
        std::move(on_done)(std::move(res), get_byte_size);
      },
      timeout);
  if (!status.ok()) {
    PS_LOG(ERROR) << "Unable to fetch bidding signals";
  }
}
}  // namespace privacy_sandbox::bidding_auction_servers
