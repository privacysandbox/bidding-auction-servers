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
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "services/common/clients/http_kv_server/seller/fake_seller_key_value_async_http_client.h"
#include "services/common/clients/http_kv_server/seller/seller_key_value_async_http_client.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/auction_scope_util.h"
#include "services/seller_frontend_service/get_component_auction_ciphertexts_reactor.h"
#include "services/seller_frontend_service/report_win_map.h"
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/select_ad_reactor_app.h"
#include "services/seller_frontend_service/select_ad_reactor_invalid.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "services/seller_frontend_service/select_auction_result_reactor.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
// Factory method to get a reactor based on the request type.
std::unique_ptr<SelectAdReactor> GetSelectAdReactor(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, server_common::Executor* executor,
    const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client,
    const ReportWinMap& report_win_map, bool enable_cancellation,
    bool enable_kanon, bool enable_buyer_private_aggregate_reporting,
    int per_adtech_paapi_contributions_limit) {
  switch (request->client_type()) {
    case CLIENT_TYPE_ANDROID:
      return std::make_unique<SelectAdReactorForApp>(
          context, request, response, executor, clients, config_client,
          report_win_map, enable_cancellation, enable_kanon);
    case CLIENT_TYPE_BROWSER:
      return std::make_unique<SelectAdReactorForWeb>(
          context, request, response, executor, clients, config_client,
          report_win_map, enable_cancellation, enable_kanon,
          enable_buyer_private_aggregate_reporting,
          per_adtech_paapi_contributions_limit);
    default:
      return std::make_unique<SelectAdReactorInvalid>(
          context, request, response, executor, clients, config_client,
          report_win_map);
  }
}

}  // namespace

std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
SellerFrontEndService::CreateV1KVClient() {
  if (config_client_.GetStringParameter(KEY_VALUE_SIGNALS_HOST) ==
      "E2E_TEST_MODE") {
    return std::make_unique<FakeSellerKeyValueAsyncHttpClient>(
        config_client_.GetStringParameter(KEY_VALUE_SIGNALS_HOST));
  } else if (config_client_.GetBooleanParameter(ENABLE_TKV_V2_BROWSER)) {
    return nullptr;
  }
  return std::make_unique<SellerKeyValueAsyncHttpClient>(
      config_client_.GetStringParameter(KEY_VALUE_SIGNALS_HOST),
      std::make_unique<MultiCurlHttpFetcherAsync>(executor_.get()), true);
}

grpc::ServerUnaryReactor* SellerFrontEndService::SelectAd(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response) {
  LogCommonMetric(request, response);
  if (request->ByteSizeLong() == 0) {
    auto reactor = std::make_unique<SelectAdReactorInvalid>(
        context, request, response, executor_.get(), clients_, config_client_,
        report_win_map_);
    reactor->Execute();
    return reactor.release();
  }
  if (AuctionScope auction_scope = GetAuctionScope(*request);
      auction_scope == AuctionScope::AUCTION_SCOPE_SERVER_TOP_LEVEL_SELLER) {
    auto reactor = std::make_unique<SelectAuctionResultReactor>(
        context, request, response, clients_, config_client_,
        enable_cancellation_,
        /*enable_buyer_private_aggregate_reporting=*/false,
        /*per_adtech_paapi_contributions_limit=*/100, enable_kanon_);
    reactor->Execute();
    return reactor.release();
  }
  std::unique_ptr<SelectAdReactor> reactor = GetSelectAdReactor(
      context, request, response, executor_.get(), clients_, config_client_,
      report_win_map_, enable_cancellation_, enable_kanon_,
      enable_buyer_private_aggregate_reporting_,
      per_adtech_paapi_contributions_limit_);
  reactor->Execute();
  return reactor.release();
}

grpc::ServerUnaryReactor* SellerFrontEndService::GetComponentAuctionCiphertexts(
    grpc::CallbackServerContext* context,
    const GetComponentAuctionCiphertextsRequest* request,
    GetComponentAuctionCiphertextsResponse* response) {
  auto reactor = std::make_unique<GetComponentAuctionCiphertextsReactor>(
      request, response, clients_.key_fetcher_manager_,
      seller_cloud_platforms_map_);
  reactor->Execute();
  return reactor.release();
}

}  // namespace privacy_sandbox::bidding_auction_servers
