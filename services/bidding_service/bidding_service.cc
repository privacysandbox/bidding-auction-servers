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
#include "src/cpp/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

void LogMetrics(const GenerateBidsRequest* request,
                GenerateBidsResponse* response) {
  auto& metric_context = metric::BiddingContextMap()->Get(request);
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

grpc::ServerUnaryReactor* BiddingService::GenerateBids(
    grpc::CallbackServerContext* context, const GenerateBidsRequest* request,
    GenerateBidsResponse* response) {
  auto scope = opentelemetry::trace::Scope(
      server_common::GetTracer()->StartSpan(kGenerateBids));
  LogMetrics(request, response);
  // Heap allocate the reactor. Deleted in reactor's OnDone call.
  auto* reactor = generate_bids_reactor_factory_(
      request, response, key_fetcher_manager_.get(), crypto_client_.get(),
      runtime_config_);
  reactor->Execute();
  return reactor;
}

grpc::ServerUnaryReactor* BiddingService::GenerateProtectedAppSignalsBids(
    grpc::CallbackServerContext* context,
    const GenerateProtectedAppSignalsBidsRequest* request,
    GenerateProtectedAppSignalsBidsResponse* response) {
  // Heap allocate the reactor. Deleted in reactor's OnDone call.
  auto* reactor = protected_app_signals_generate_bids_reactor_factory_(
      context, request, runtime_config_, response, key_fetcher_manager_.get(),
      crypto_client_.get(), http_ad_retrieval_async_client_.get());
  reactor->Execute();
  return reactor;
}

}  // namespace privacy_sandbox::bidding_auction_servers
