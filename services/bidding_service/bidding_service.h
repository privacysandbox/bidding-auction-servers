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

#ifndef SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_H_
#define SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_H_

#include <memory>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/common/clients/async_grpc/default_async_grpc_client.h"
#include "services/common/clients/http/multi_curl_http_fetcher_async.h"
#include "services/common/clients/kv_server/kv_async_client.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kGenerateBids[] = "GenerateBids";

using GenerateBidsReactorFactory = absl::AnyInvocable<BaseGenerateBidsReactor<
    GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
    GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse>*(
    grpc::CallbackServerContext* context, const GenerateBidsRequest* request,
    GenerateBidsResponse* response,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config)>;

using ProtectedAppSignalsGenerateBidsReactorFactory = absl::AnyInvocable<
    BaseGenerateBidsReactor<GenerateProtectedAppSignalsBidsRequest,
                            GenerateProtectedAppSignalsBidsRequest::
                                GenerateProtectedAppSignalsBidsRawRequest,
                            GenerateProtectedAppSignalsBidsResponse,
                            GenerateProtectedAppSignalsBidsResponse::
                                GenerateProtectedAppSignalsBidsRawResponse>*(
        grpc::CallbackServerContext* context,
        const GenerateProtectedAppSignalsBidsRequest* request,
        const BiddingServiceRuntimeConfig& runtime_config,
        GenerateProtectedAppSignalsBidsResponse* response,
        server_common::KeyFetcherManagerInterface* key_fetcher_manager,
        CryptoClientWrapperInterface* crypto_client,
        KVAsyncClient* ad_retrieval_client, KVAsyncClient* kv_async_client,
        EgressSchemaCache* egress_schema_cache,
        EgressSchemaCache* limted_egress_schema_cache)>;

// BiddingService implements business logic for generating a bid. It takes
// input from the BuyerFrontEndService.
// The bid generation uses proprietary AdTech code that runs in a secure privacy
// sandbox inside a secure Virtual Machine. The implementation dispatches to
// V8 to run the AdTech code.
class BiddingService final : public Bidding::CallbackService {
 public:
  explicit BiddingService(
      GenerateBidsReactorFactory generate_bids_reactor_factory,
      std::unique_ptr<server_common::KeyFetcherManagerInterface>
          key_fetcher_manager,
      std::unique_ptr<CryptoClientWrapperInterface> crypto_client,
      BiddingServiceRuntimeConfig runtime_config,
      ProtectedAppSignalsGenerateBidsReactorFactory
          protected_app_signals_generate_bids_reactor_factory,
      std::unique_ptr<KVAsyncClient> ad_retrieval_async_client = nullptr,
      std::unique_ptr<KVAsyncClient> kv_async_client = nullptr,
      std::unique_ptr<EgressSchemaCache> egress_schema_cache = nullptr,
      std::unique_ptr<EgressSchemaCache> limited_egress_schema_cache = nullptr)
      : generate_bids_reactor_factory_(
            std::move(generate_bids_reactor_factory)),
        key_fetcher_manager_(std::move(key_fetcher_manager)),
        crypto_client_(std::move(crypto_client)),
        runtime_config_(std::move(runtime_config)),
        protected_app_signals_generate_bids_reactor_factory_(
            std::move(protected_app_signals_generate_bids_reactor_factory)),
        ad_retrieval_async_client_(std::move(ad_retrieval_async_client)),
        kv_async_client_(std::move(kv_async_client)),
        egress_schema_cache_(std::move(egress_schema_cache)),
        limited_egress_schema_cache_(std::move(limited_egress_schema_cache)) {
    if (ad_retrieval_async_client_ == nullptr &&
        !runtime_config_.tee_ad_retrieval_kv_server_addr.empty()) {
      auto ad_retrieval_stub =
          kv_server::v2::KeyValueService::NewStub(CreateChannel(
              runtime_config_.tee_ad_retrieval_kv_server_addr,
              /*compression=*/true,
              /*secure=*/runtime_config.ad_retrieval_kv_server_egress_tls,
              /*grpc_arg_default_authority=*/
              runtime_config
                  .tee_ad_retrieval_kv_server_grpc_arg_default_authority));
      ad_retrieval_async_client_ = std::make_unique<KVAsyncGrpcClient>(
          key_fetcher_manager_.get(), std::move(ad_retrieval_stub));
    }
    if (kv_async_client_ == nullptr &&
        !runtime_config_.tee_kv_server_addr.empty()) {
      auto kv_async_client_stub =
          kv_server::v2::KeyValueService::NewStub(CreateChannel(
              runtime_config_.tee_kv_server_addr,
              /*compression=*/true,
              /*secure=*/runtime_config.kv_server_egress_tls,
              /*grpc_arg_default_authority=*/
              runtime_config.tee_kv_server_grpc_arg_default_authority));
      kv_async_client_ = std::make_unique<KVAsyncGrpcClient>(
          key_fetcher_manager_.get(), std::move(kv_async_client_stub));
    }
  }

  // Generates bids for ad candidates owned by the Buyer in an ad auction
  // orchestrated by the SellerFrontEndService.
  //
  // This is the API that is accessed by the BuyerFrontEndService. This is the
  // async RPC endpoint that will dispatch to V8 to execute the provided
  // AdTech code.
  //
  // context: Standard gRPC-owned testing utility parameter.
  // request: Pointer to GenerateBidsRequest. The request is encrypted using
  // Hybrid Public Key Encryption (HPKE).
  // response: Pointer to GenerateBidsResponse. The response is encrypted
  // using HPKE.
  // return: a ServerUnaryReactor that is used by the gRPC library
  grpc::ServerUnaryReactor* GenerateBids(
      grpc::CallbackServerContext* context, const GenerateBidsRequest* request,
      GenerateBidsResponse* response) override;

  // Generates bids for protected app signals.
  grpc::ServerUnaryReactor* GenerateProtectedAppSignalsBids(
      grpc::CallbackServerContext* context,
      const GenerateProtectedAppSignalsBidsRequest* request,
      GenerateProtectedAppSignalsBidsResponse* response) override;

 private:
  GenerateBidsReactorFactory generate_bids_reactor_factory_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_;
  BiddingServiceRuntimeConfig runtime_config_;
  ProtectedAppSignalsGenerateBidsReactorFactory
      protected_app_signals_generate_bids_reactor_factory_;
  std::unique_ptr<KVAsyncClient> ad_retrieval_async_client_;
  std::unique_ptr<KVAsyncClient> kv_async_client_;
  std::unique_ptr<EgressSchemaCache> egress_schema_cache_;
  std::unique_ptr<EgressSchemaCache> limited_egress_schema_cache_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_H_
