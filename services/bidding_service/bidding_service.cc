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

#include "services/bidding_service/bidding_service.h"

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "services/bidding_service/generate_bids_reactor.h"
#include "services/common/metric/server_definition.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

grpc::ServerUnaryReactor* BiddingService::GenerateBids(
    grpc::CallbackServerContext* context, const GenerateBidsRequest* request,
    GenerateBidsResponse* response) {
  LogCommonMetric(static_cast<const google::protobuf::Message*>(request),
                  response);
  // Heap allocate the reactor. Deleted in reactor's OnDone call.
  auto* reactor = generate_bids_reactor_factory_(
      context, request, response, key_fetcher_manager_.get(),
      crypto_client_.get(), runtime_config_);
  reactor->Execute();
  return reactor;
}

grpc::ServerUnaryReactor* BiddingService::GenerateProtectedAppSignalsBids(
    grpc::CallbackServerContext* context,
    const GenerateProtectedAppSignalsBidsRequest* request,
    GenerateProtectedAppSignalsBidsResponse* response) {
  LogCommonMetric(static_cast<const google::protobuf::Message*>(request),
                  response);
  // Heap allocate the reactor. Deleted in reactor's OnDone call.
  auto* reactor = protected_app_signals_generate_bids_reactor_factory_(
      context, request, runtime_config_, response, key_fetcher_manager_.get(),
      crypto_client_.get(), ad_retrieval_async_client_.get(),
      kv_async_client_.get(),
      egress_schema_cache_ != nullptr ? egress_schema_cache_.get() : nullptr,
      limited_egress_schema_cache_ != nullptr
          ? limited_egress_schema_cache_.get()
          : nullptr);
  reactor->Execute();
  return reactor;
}

}  // namespace privacy_sandbox::bidding_auction_servers
