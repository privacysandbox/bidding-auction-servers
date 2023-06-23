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

#include "services/buyer_frontend_service/buyer_frontend_service.h"

#include <memory>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "services/buyer_frontend_service/get_bids_unary_reactor.h"
#include "services/common/metric/server_definition.h"
#include "src/cpp/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
void LogMetrics(const GetBidsRequest* request) {
  auto& metric_context = metric::BfeContextMap()->Get(request);
  LogIfError(
      metric_context
          .LogUpDownCounter<server_common::metric::kTotalRequestCount>(1));
  LogIfError(
      metric_context
          .LogHistogramDeferred<server_common::metric::kServerTotalTimeMs>(
              [start = absl::Now()]() -> int {
                return (absl::Now() - start) / absl::Milliseconds(1);
              }));
}
}  // namespace

BuyerFrontEndService::BuyerFrontEndService(
    std::unique_ptr<BiddingSignalsAsyncProvider> bidding_signals_async_provider,
    std::unique_ptr<BiddingAsyncClient> bidding_async_client,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client, const GetBidsConfig config,
    bool enable_benchmarking)
    : bidding_signals_async_provider_(
          std::move(bidding_signals_async_provider)),
      bidding_async_client_(std::move(bidding_async_client)),
      key_fetcher_manager_(key_fetcher_manager),
      crypto_client_(crypto_client),
      config_(std::move(config)),
      enable_benchmarking_(enable_benchmarking) {}

grpc::ServerUnaryReactor* BuyerFrontEndService::GetBids(
    grpc::CallbackServerContext* context, const GetBidsRequest* request,
    GetBidsResponse* response) {
  auto scope = opentelemetry::trace::Scope(
      server_common::GetTracer()->StartSpan("GetBids"));
  LogMetrics(request);

  VLOG(2) << "\nGetBidsRequest:\n" << request->DebugString();
  if (VLOG_IS_ON(2)) {
    VLOG(2) << "Headers:\n";
    for (const auto& it : context->client_metadata()) {
      VLOG(2) << it.first << " : " << it.second << "\n";
    }
  }

  // Will be deleted in onDone
  auto reactor = std::make_unique<GetBidsUnaryReactor>(
      *context, *request, *response, *bidding_signals_async_provider_,
      *bidding_async_client_, config_, key_fetcher_manager_, crypto_client_,
      enable_benchmarking_);
  reactor->Execute();
  return reactor.release();
}
}  // namespace privacy_sandbox::bidding_auction_servers
