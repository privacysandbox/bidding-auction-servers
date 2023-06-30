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

#include "services/seller_frontend_service/seller_frontend_service.h"

#include <utility>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "glog/logging.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "services/common/metric/server_definition.h"
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/select_ad_reactor_app.h"
#include "services/seller_frontend_service/select_ad_reactor_invalid_client.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "src/cpp/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

void LogMetrics(const SelectAdRequest* request, SelectAdResponse* response) {
  auto& metric_context = metric::SfeContextMap()->Get(request);
  LogIfError(
      metric_context
          .LogUpDownCounter<server_common::metric::kTotalRequestCount>(1));
  LogIfError(
      metric_context
          .LogHistogramDeferred<server_common::metric::kServerTotalTimeMs>(
              [start = absl::Now()]() -> int {
                return (absl::Now() - start) / absl::Milliseconds(1);
              }));
  LogIfError(metric_context.LogHistogram<server_common::metric::kRequestByte>(
      (int)request->ByteSizeLong()));
  LogIfError(
      metric_context.LogHistogramDeferred<server_common::metric::kResponseByte>(
          [response]() -> int { return response->ByteSizeLong(); }));
  LogIfError(metric_context.LogUpDownCounterDeferred<
             server_common::metric::kTotalRequestFailedCount>(
      [&metric_context]() -> int {
        return metric_context.is_request_successful() ? 0 : 1;
      }));
}

// Factory method to get a reactor based on the request type.
std::unique_ptr<SelectAdReactor> GetReactorForRequest(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client) {
  switch (request->client_type()) {
    case SelectAdRequest::ANDROID:
      return std::make_unique<SelectAdReactorForApp>(context, request, response,
                                                     clients, config_client);
    case SelectAdRequest::BROWSER:
      return std::make_unique<SelectAdReactorForWeb>(context, request, response,
                                                     clients, config_client);
    default:
      return std::make_unique<SelectAdReactorInvalidClient>(
          context, request, response, clients, config_client);
  }
}

}  // namespace

grpc::ServerUnaryReactor* SellerFrontEndService::SelectAd(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response) {
  auto scope = opentelemetry::trace::Scope(
      server_common::GetTracer()->StartSpan("SelectAd"));
  LogMetrics(request, response);

  VLOG(2) << "\nSelectAdRequest:\n" << request->DebugString();
  if (VLOG_IS_ON(2)) {
    VLOG(2) << "Headers:\n";
    for (const auto& it : context->client_metadata()) {
      VLOG(2) << it.first << " : " << it.second << "\n";
    }
  }

  auto reactor = GetReactorForRequest(context, request, response, clients_,
                                      config_client_);
  reactor->Execute();
  return reactor.release();
}

}  // namespace privacy_sandbox::bidding_auction_servers
