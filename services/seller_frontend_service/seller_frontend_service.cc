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
#include "services/seller_frontend_service/select_ad_reactor.h"
#include "services/seller_frontend_service/select_ad_reactor_app.h"
#include "services/seller_frontend_service/select_ad_reactor_invalid_client.h"
#include "services/seller_frontend_service/select_ad_reactor_web.h"
#include "src/telemetry/telemetry.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
// Factory method to get a reactor based on the request type.
std::unique_ptr<SelectAdReactor> GetReactorForRequest(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client) {
  switch (request->client_type()) {
    case CLIENT_TYPE_ANDROID:
      return std::make_unique<SelectAdReactorForApp>(context, request, response,
                                                     clients, config_client);
    case CLIENT_TYPE_BROWSER:
      return std::make_unique<SelectAdReactorForWeb>(context, request, response,
                                                     clients, config_client);
    default:
      return std::make_unique<SelectAdReactorInvalidClient>(
          context, request, response, clients, config_client);
  }
}

}  // namespace

std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
SellerFrontEndService::CreateKVClient() {
  if (config_client_.GetStringParameter(KEY_VALUE_SIGNALS_HOST) ==
      "E2E_TEST_MODE") {
    return std::make_unique<FakeSellerKeyValueAsyncHttpClient>(
        config_client_.GetStringParameter(KEY_VALUE_SIGNALS_HOST));
  } else {
    return std::make_unique<SellerKeyValueAsyncHttpClient>(
        config_client_.GetStringParameter(KEY_VALUE_SIGNALS_HOST),
        std::make_unique<MultiCurlHttpFetcherAsync>(executor_.get()), true);
  }
}

grpc::ServerUnaryReactor* SellerFrontEndService::SelectAd(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response) {
  LogCommonMetric(request, response);
  auto reactor = GetReactorForRequest(context, request, response, clients_,
                                      config_client_);
  reactor->Execute();
  return reactor.release();
}

}  // namespace privacy_sandbox::bidding_auction_servers
