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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_SELLER_FRONTEND_SERVICE_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_SELLER_FRONTEND_SERVICE_H_

#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/clients/auction_server/scoring_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client_factory.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/reporters/async_reporter.h"
#include "services/seller_frontend_service/providers/http_scoring_signals_async_provider.h"
#include "services/seller_frontend_service/providers/scoring_signals_async_provider.h"
#include "services/seller_frontend_service/runtime_flags.h"
#include "services/seller_frontend_service/util/config_param_parser.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/cpp/concurrent/event_engine_executor.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// This a utility class that acts as a wrapper for the clients that are used
// by SellerFrontEndService.
struct ClientRegistry {
  const ScoringSignalsAsyncProvider& scoring_signals_async_provider;
  const ScoringAsyncClient& scoring;
  const ClientFactory<BuyerFrontEndAsyncClient, absl::string_view>&
      buyer_factory;
  server_common::KeyFetcherManagerInterface& key_fetcher_manager_;
  std::unique_ptr<AsyncReporter> reporting;
};

// SellerFrontEndService implements business logic to orchestrate requests
// to the Buyers participating in an ad auction. In addition, fetch the AdTech's
// proprietary code for scoring ads, looks up realtime seller's signals
// required for ad auction, and calls the Auction Service to execute AdTech's
// code in a secure privacy sandbox inside a secure Virtual Machine.
class SellerFrontEndService final : public SellerFrontEnd::CallbackService {
 public:
  SellerFrontEndService(
      const TrustedServersConfigClient* config_client,
      std::unique_ptr<server_common::KeyFetcherManagerInterface>
          key_fetcher_manager,
      std::unique_ptr<CryptoClientWrapperInterface> crypto_client)
      : config_client_(*config_client),
        key_fetcher_manager_(std::move(key_fetcher_manager)),
        crypto_client_(std::move(crypto_client)),
        executor_(std::make_unique<server_common::EventEngineExecutor>(
            config_client_.GetBooleanParameter(CREATE_NEW_EVENT_ENGINE)
                ? grpc_event_engine::experimental::CreateEventEngine()
                : grpc_event_engine::experimental::GetDefaultEventEngine())),
        scoring_signals_async_provider_(
            std::make_unique<HttpScoringSignalsAsyncProvider>(
                CreateKVClient(), config_client_.GetBooleanParameter(
                                      ENABLE_PROTECTED_APP_SIGNALS))),
        scoring_(std::make_unique<ScoringAsyncGrpcClient>(
            key_fetcher_manager_.get(), crypto_client_.get(),
            AuctionServiceClientConfig{
                .server_addr =
                    config_client_.GetStringParameter(AUCTION_SERVER_HOST)
                        .data(),
                .compression = config_client_.GetBooleanParameter(
                    ENABLE_AUCTION_COMPRESSION),
                .secure_client =
                    config_client_.GetBooleanParameter(AUCTION_EGRESS_TLS),
                .encryption_enabled =
                    config_client_.GetBooleanParameter(ENABLE_ENCRYPTION)})),
        buyer_factory_([this]() {
          absl::StatusOr<absl::flat_hash_map<std::string, BuyerServiceEndpoint>>
              ig_owner_to_bfe_domain_map = ParseIgOwnerToBfeDomainMap(
                  config_client_.GetStringParameter(BUYER_SERVER_HOSTS));
          CHECK_OK(ig_owner_to_bfe_domain_map);
          return std::make_unique<BuyerFrontEndAsyncClientFactory>(
              *ig_owner_to_bfe_domain_map, key_fetcher_manager_.get(),
              crypto_client_.get(),
              BuyerServiceClientConfig{
                  .compression = config_client_.GetBooleanParameter(
                      ENABLE_BUYER_COMPRESSION),
                  .secure_client =
                      config_client_.GetBooleanParameter(BUYER_EGRESS_TLS),
                  .encryption_enabled =
                      config_client_.GetBooleanParameter(ENABLE_ENCRYPTION)});
        }()),
        clients_{
            *scoring_signals_async_provider_, *scoring_, *buyer_factory_,
            *key_fetcher_manager_,
            std::make_unique<AsyncReporter>(
                std::make_unique<MultiCurlHttpFetcherAsync>(executor_.get()))} {
  }

  SellerFrontEndService(const TrustedServersConfigClient* config_client,
                        ClientRegistry clients)
      : config_client_(*config_client), clients_(std::move(clients)) {}

  std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
  CreateKVClient();

  // Selects a winning ad by running an ad auction.
  //
  // This is an rpc endpoint which will lead to further requests (rpc and http)
  // to other endpoints.
  //
  // context: Standard gRPC-owned testing utility parameter.
  // request: Pointer to SelectAdRequest. The request is encrypted using
  // Hybrid Public Key Encryption (HPKE).
  // response: Pointer to SelectAdResponse. The response is encrypted
  // using HPKE.
  // return: a ServerUnaryReactor that is used by the gRPC library
  grpc::ServerUnaryReactor* SelectAd(
      grpc::CallbackServerContext* context,
      const bidding_auction_servers::SelectAdRequest* request,
      bidding_auction_servers::SelectAdResponse* response) override;

 private:
  const TrustedServersConfigClient& config_client_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_;
  std::unique_ptr<server_common::Executor> executor_;
  std::unique_ptr<ScoringSignalsAsyncProvider> scoring_signals_async_provider_;
  std::unique_ptr<ScoringAsyncClient> scoring_;
  std::unique_ptr<ClientFactory<BuyerFrontEndAsyncClient, absl::string_view>>
      buyer_factory_;
  const ClientRegistry clients_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELLER_FRONTEND_SERVICE_H_
