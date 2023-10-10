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
void LogMetrics(const GetBidsRequest* request, GetBidsResponse* response) {
  auto& metric_context = metric::BfeContextMap()->Get(request);
  LogIfError(
      metric_context
          .LogUpDownCounter<server_common::metrics::kTotalRequestCount>(1));
  LogIfError(
      metric_context
          .LogHistogramDeferred<server_common::metrics::kServerTotalTimeMs>(
              [start = absl::Now()]() -> int {
                return (absl::Now() - start) / absl::Milliseconds(1);
              }));
  LogIfError(metric_context.LogHistogram<server_common::metrics::kRequestByte>(
      (int)request->ByteSizeLong()));

  LogIfError(metric_context
                 .LogHistogramDeferred<server_common::metrics::kResponseByte>(
                     [response]() -> int { return response->ByteSizeLong(); }));
  LogIfError(metric_context.LogUpDownCounterDeferred<
             server_common::metrics::kTotalRequestFailedCount>(
      [&metric_context]() -> int {
        return metric_context.is_request_successful() ? 0 : 1;
      }));
}
}  // namespace

BuyerFrontEndService::BuyerFrontEndService(
    std::unique_ptr<BiddingSignalsAsyncProvider> bidding_signals_async_provider,
    const BiddingServiceClientConfig& client_config,
    std::unique_ptr<server_common::KeyFetcherManagerInterface>
        key_fetcher_manager,
    std::unique_ptr<CryptoClientWrapperInterface> crypto_client,
    const GetBidsConfig config, bool enable_benchmarking)
    : bidding_signals_async_provider_(
          std::move(bidding_signals_async_provider)),
      config_(std::move(config)),
      enable_benchmarking_(enable_benchmarking),
      key_fetcher_manager_(std::move(key_fetcher_manager)),
      crypto_client_(std::move(crypto_client)),
      stub_(Bidding::NewStub(CreateChannel(client_config.server_addr,
                                           client_config.compression,
                                           client_config.secure_client))),
      bidding_async_client_(std::make_unique<BiddingAsyncGrpcClient>(
          key_fetcher_manager_.get(), crypto_client_.get(), client_config,
          stub_.get())) {
  if (config_.is_protected_app_signals_enabled) {
    protected_app_signals_bidding_async_client_ =
        std::make_unique<ProtectedAppSignalsBiddingAsyncGrpcClient>(
            key_fetcher_manager_.get(), crypto_client_.get(), client_config,
            stub_.get());
  }
}

BuyerFrontEndService::BuyerFrontEndService(ClientRegistry client_registry,
                                           GetBidsConfig config,
                                           bool enable_benchmarking)
    : bidding_signals_async_provider_(
          std::move(client_registry.bidding_signals_async_provider)),
      config_(std::move(config)),
      enable_benchmarking_(enable_benchmarking),
      key_fetcher_manager_(std::move(client_registry.key_fetcher_manager)),
      crypto_client_(std::move(client_registry.crypto_client)),
      bidding_async_client_(std::move(client_registry.bidding_async_client)),
      protected_app_signals_bidding_async_client_(std::move(
          client_registry.protected_app_signals_bidding_async_client)) {}

grpc::ServerUnaryReactor* BuyerFrontEndService::GetBids(
    grpc::CallbackServerContext* context, const GetBidsRequest* request,
    GetBidsResponse* response) {
  auto scope = opentelemetry::trace::Scope(
      server_common::GetTracer()->StartSpan("GetBids"));
  LogMetrics(request, response);

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
      *bidding_async_client_, config_,
      protected_app_signals_bidding_async_client_.get(),
      key_fetcher_manager_.get(), crypto_client_.get(), enable_benchmarking_);
  reactor->Execute();
  return reactor.release();
}
}  // namespace privacy_sandbox::bidding_auction_servers
