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
#include "absl/flags/flag.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/aliases.h"
#include "services/common/clients/auction_server/scoring_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client_factory.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/clients/kv_server/kv_async_client.h"
#include "services/common/feature_flags.h"
#include "services/common/reporters/async_reporter.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"
#include "services/seller_frontend_service/providers/http_scoring_signals_async_provider.h"
#include "services/seller_frontend_service/providers/scoring_signals_async_provider.h"
#include "services/seller_frontend_service/report_win_map.h"
#include "services/seller_frontend_service/runtime_flags.h"
#include "services/seller_frontend_service/util/config_param_parser.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// Create seller cloud platform map.
static inline absl::flat_hash_map<std::string, server_common::CloudPlatform>
ParseSellerCloudPlarformMap(absl::string_view seller_platforms_map_json) {
  auto seller_cloud_platform_parse_status =
      ParseSellerToCloudPlatformInMap(seller_platforms_map_json);
  CHECK_OK(seller_cloud_platform_parse_status);
  return *seller_cloud_platform_parse_status;
}

// This a utility class that acts as a wrapper for the clients that are used
// by SellerFrontEndService.
struct ClientRegistry {
  absl::Nullable<const ScoringSignalsAsyncProvider* const>
      scoring_signals_async_provider;
  ScoringAsyncClient& scoring;
  const ClientFactory<BuyerFrontEndAsyncClient, absl::string_view>&
      buyer_factory;
  absl::Nullable<KVAsyncClient* const> kv_async_client;
  server_common::KeyFetcherManagerInterface& key_fetcher_manager_;
  CryptoClientWrapperInterface* const crypto_client_ptr_;
  std::unique_ptr<AsyncReporter> reporting;
  std::unique_ptr<KAnonCacheManagerInterface> k_anon_cache_manager;
  std::unique_ptr<InvokedBuyersCache> invoked_buyers_cache;
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
      std::unique_ptr<CryptoClientWrapperInterface> crypto_client,
      ReportWinMap report_win_map,
      std::unique_ptr<KAnonCacheManagerInterface> k_anon_cache_manager =
          nullptr,
      std::unique_ptr<InvokedBuyersCache> invoked_buyers_cache = nullptr)
      : config_client_(*config_client),
        key_fetcher_manager_(std::move(key_fetcher_manager)),
        crypto_client_(std::move(crypto_client)),
        executor_(std::make_unique<server_common::EventEngineExecutor>(
            config_client_.GetBooleanParameter(CREATE_NEW_EVENT_ENGINE)
                ? grpc_event_engine::experimental::CreateEventEngine()
                : grpc_event_engine::experimental::GetDefaultEventEngine())),
        scoring_signals_async_provider_(
            config_client_.GetStringParameter(SCORING_SIGNALS_FETCH_MODE) ==
                    kSignalsNotFetched
                ? nullptr
                : std::make_unique<HttpScoringSignalsAsyncProvider>(
                      CreateV1KVClient(), config_client_.GetBooleanParameter(
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
                .grpc_arg_default_authority =
                    config_client_
                        .GetStringParameter(GRPC_ARG_DEFAULT_AUTHORITY_VAL)
                        .data()})),
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
                  .chaffing_enabled =
                      config_client_.GetBooleanParameter(ENABLE_CHAFFING)});
        }()),
        kv_async_client_(
            config_client_.GetStringParameter(SCORING_SIGNALS_FETCH_MODE) ==
                    kSignalsNotFetched
                ? nullptr
                : std::make_unique<KVAsyncGrpcClient>(
                      key_fetcher_manager_.get(),
                      kv_server::v2::KeyValueService::NewStub(CreateChannel(
                          config_client_.GetStringParameter(
                              TRUSTED_KEY_VALUE_V2_SIGNALS_HOST),
                          /*compression=*/true,
                          /*secure=*/
                          config_client_.GetBooleanParameter(TKV_EGRESS_TLS),
                          /*grpc_arg_default_authority=*/
                          config_client_
                              .GetStringParameter(
                                  GRPC_ARG_DEFAULT_AUTHORITY_VAL)
                              .data())))),
        clients_{
            scoring_signals_async_provider_.get(),
            *scoring_,
            *buyer_factory_,
            kv_async_client_.get(),
            *key_fetcher_manager_,
            crypto_client_.get(),
            std::make_unique<AsyncReporter>(
                std::make_unique<MultiCurlHttpFetcherAsync>(executor_.get())),
            std::move(k_anon_cache_manager),
        },
        enable_cancellation_(absl::GetFlag(FLAGS_enable_cancellation)),
        enable_kanon_(absl::GetFlag(FLAGS_enable_kanon)),
        report_win_map_(std::move(report_win_map)),
        enable_buyer_private_aggregate_reporting_(
            absl::GetFlag(FLAGS_enable_buyer_private_aggregate_reporting)),
        per_adtech_paapi_contributions_limit_(
            absl::GetFlag(FLAGS_per_adtech_paapi_contributions_limit)) {
    if (config_client_.HasParameter(SELLER_CLOUD_PLATFORMS_MAP)) {
      seller_cloud_platforms_map_ = ParseSellerCloudPlarformMap(
          config_client_.GetStringParameter(SELLER_CLOUD_PLATFORMS_MAP));
    }
  }

  SellerFrontEndService(const TrustedServersConfigClient* config_client,
                        ClientRegistry clients)
      : config_client_(*config_client),
        executor_(std::make_unique<server_common::EventEngineExecutor>(
            config_client_.GetBooleanParameter(CREATE_NEW_EVENT_ENGINE)
                ? grpc_event_engine::experimental::CreateEventEngine()
                : grpc_event_engine::experimental::GetDefaultEventEngine())),
        clients_(std::move(clients)),
        enable_cancellation_(absl::GetFlag(FLAGS_enable_cancellation)),
        enable_kanon_(absl::GetFlag(FLAGS_enable_kanon)),
        enable_buyer_private_aggregate_reporting_(
            absl::GetFlag(FLAGS_enable_buyer_private_aggregate_reporting)),
        per_adtech_paapi_contributions_limit_(
            absl::GetFlag(FLAGS_per_adtech_paapi_contributions_limit)) {
    if (config_client_.HasParameter(SELLER_CLOUD_PLATFORMS_MAP)) {
      seller_cloud_platforms_map_ = ParseSellerCloudPlarformMap(
          config_client_.GetStringParameter(SELLER_CLOUD_PLATFORMS_MAP));
    }
  }

  std::unique_ptr<AsyncClient<GetSellerValuesInput, GetSellerValuesOutput>>
  CreateV1KVClient();

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

  // Selects a winning ad by running an ad auction.
  //
  // This is an rpc endpoint which will lead to further requests (rpc and http)
  // to other endpoints.
  //
  // context: Standard gRPC-owned testing utility parameter.
  // request: Pointer to GetComponentAuctionCiphertextsRequest.
  // The request contains a ciphertext encrypted using
  // Hybrid Public Key Encryption (HPKE).
  // response: Pointer to GetComponentAuctionCiphertextsResponse.
  // The response contains the same payload encrypted to unique ciphertexts
  // using HPKE.
  // return: a ServerUnaryReactor that is used by the gRPC library
  grpc::ServerUnaryReactor* GetComponentAuctionCiphertexts(
      grpc::CallbackServerContext* context,
      const bidding_auction_servers::GetComponentAuctionCiphertextsRequest*
          request,
      bidding_auction_servers::GetComponentAuctionCiphertextsResponse* response)
      override;

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
  std::unique_ptr<KVAsyncGrpcClient> kv_async_client_;
  const ClientRegistry clients_;
  absl::flat_hash_map<std::string, server_common::CloudPlatform>
      seller_cloud_platforms_map_;
  const bool enable_cancellation_;
  const bool enable_kanon_;
  ReportWinMap report_win_map_;
  const bool enable_buyer_private_aggregate_reporting_;
  int per_adtech_paapi_contributions_limit_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELLER_FRONTEND_SERVICE_H_
