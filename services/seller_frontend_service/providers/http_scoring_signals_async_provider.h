//  Copyright 2022 Google LLC
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

#ifndef SERVICES_SFE_PROVIDERS_HTTP_SCORING_SIGNALS_ASYNC_PROVIDER_H_
#define SERVICES_SFE_PROVIDERS_HTTP_SCORING_SIGNALS_ASYNC_PROVIDER_H_

#include <memory>

#include "services/common/clients/async_client.h"
#include "services/common/clients/http_kv_server/seller/seller_key_value_async_http_client.h"
#include "services/seller_frontend_service/providers/scoring_signals_async_provider.h"

namespace privacy_sandbox::bidding_auction_servers {

class HttpScoringSignalsAsyncProvider final
    : public ScoringSignalsAsyncProvider {
 public:
  explicit HttpScoringSignalsAsyncProvider(
      std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>,
      bool enable_protected_app_signals = false);

  // HttpScoringSignalsAsyncProvider is neither copyable nor movable.
  HttpScoringSignalsAsyncProvider(const HttpScoringSignalsAsyncProvider&) =
      delete;
  HttpScoringSignalsAsyncProvider& operator=(
      const HttpScoringSignalsAsyncProvider&) = delete;

  // Obtains bidding signals by batching the keys in the buyer input
  // interest groups. When the output from all Key Value servers is obtained,
  // the on_done function is invoked with the results.
  void Get(const ScoringSignalsRequest& scoring_signals_request,
           absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<

                                       ScoringSignals>>,
                                   GetByteSize) &&>
               on_done,
           absl::Duration timeout,
           RequestContext context = NoOpContext()) const override;

 private:
  std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
      http_seller_kv_async_client_;
  const bool enable_protected_app_signals_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SFE_PROVIDERS_HTTP_SCORING_SIGNALS_ASYNC_PROVIDER_H_
