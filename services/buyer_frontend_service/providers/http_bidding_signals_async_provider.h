/*
 * Copyright 2022 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_BFE_SERVICE_PROVIDERS_HTTP_BIDDING_SIGNALS_ASYNC_PROVIDER_H_
#define SERVICES_BFE_SERVICE_PROVIDERS_HTTP_BIDDING_SIGNALS_ASYNC_PROVIDER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/synchronization/blocking_counter.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/clients/http_kv_server/buyer/buyer_key_value_async_http_client.h"

namespace privacy_sandbox::bidding_auction_servers {

class HttpBiddingSignalsAsyncProvider final
    : public BiddingSignalsAsyncProvider {
 public:
  explicit HttpBiddingSignalsAsyncProvider(
      std::unique_ptr<AsyncClient<GetBuyerValuesInput, GetBuyerValuesOutput>>
          http_buyer_kv_async_client);

  // HttpBiddingSignalsAsyncProvider is neither copyable nor movable.
  HttpBiddingSignalsAsyncProvider(const HttpBiddingSignalsAsyncProvider&) =
      delete;
  HttpBiddingSignalsAsyncProvider& operator=(
      const HttpBiddingSignalsAsyncProvider&) = delete;

  // Obtains bidding signals by batching the keys in the buyer input
  // interest groups. When the output from all Key Value servers is obtained,
  // the on_done function is invoked with the results.
  void Get(const BiddingSignalsRequest& get_bids_raw_request,
           absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<

                                       BiddingSignals>>,
                                   GetByteSize) &&>
               on_done,
           absl::Duration timeout,
           RequestContext context = NoOpContext()) const override;

 private:
  std::unique_ptr<AsyncClient<GetBuyerValuesInput, GetBuyerValuesOutput>>
      http_buyer_kv_async_client_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BFE_SERVICE_PROVIDERS_HTTP_BIDDING_SIGNALS_ASYNC_PROVIDER_H_
