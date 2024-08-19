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

#include "services/auction_service/auction_service.h"

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "services/common/metric/server_definition.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

grpc::ServerUnaryReactor* AuctionService::ScoreAds(
    grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
    ScoreAdsResponse* response) {
  LogCommonMetric(request, response);
  // Heap allocate the reactor. Deleted in reactor's OnDone call.
  auto reactor = score_ads_reactor_factory_(
      context, request, response, key_fetcher_manager_.get(),
      crypto_client_.get(), runtime_config_);
  reactor->Execute();
  return reactor.release();
}
}  // namespace privacy_sandbox::bidding_auction_servers
