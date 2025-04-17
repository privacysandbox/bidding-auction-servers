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

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_H_

#include <memory>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/buyer_frontend_service/data/get_bids_config.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/clients/kv_server/kv_async_client.h"
#include "src/concurrent/executor.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// This a utility class that acts as a wrapper for the clients that are used
// by BuyerFrontEndService.
struct ClientRegistry {
  std::unique_ptr<BiddingSignalsAsyncProvider> bidding_signals_async_provider;
  std::unique_ptr<BiddingAsyncClient> bidding_async_client;
  std::unique_ptr<ProtectedAppSignalsBiddingAsyncClient>
      protected_app_signals_bidding_async_client;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client;
  std::unique_ptr<KVAsyncClient> kv_async_client;
};

// BuyerFrontEndService provides the async server implementation to be used
// by Demand Side/Buyers to provide bids to the Seller Frontend Service
// during an Ad auction. The server looks up realtime buyer's signals and calls
// Bidding Service to execute AdTech's code in a secure privacy sandbox inside a
// secure Virtual Machine for generating the bids for the auction.
class BuyerFrontEndService final : public BuyerFrontEnd::CallbackService {
 public:
  explicit BuyerFrontEndService(
      std::unique_ptr<BiddingSignalsAsyncProvider>
          bidding_signals_async_provider,
      const BiddingServiceClientConfig& client_config,
      std::unique_ptr<server_common::KeyFetcherManagerInterface>
          key_fetcher_manager,
      std::unique_ptr<CryptoClientWrapperInterface> crypto_client,
      std::unique_ptr<KVAsyncClient> kv_async_client,
      const GetBidsConfig config, server_common::Executor& executor,
      bool enable_benchmarking = false);

  explicit BuyerFrontEndService(ClientRegistry client_registry,
                                const GetBidsConfig config,
                                server_common::Executor& executor,
                                bool enable_benchmarking = false);

  // Returns bid for the top eligible ad candidate.
  //
  // This is the API that can be used by the Seller Frontend Service.
  // This is an rpc endpoint which will lead to further requests (rpc and http)
  // to other endpoints.
  //
  // context: Standard gRPC-owned testing utility parameter.
  // request: Pointer to GetBidsRequest. The request is encrypted using
  // Hybrid Public Key Encryption (HPKE).
  // response: Pointer to GetBidsResponse. The response is encrypted using HPKE.
  // return: a ServerUnaryReactor used by the gRPC library to communicate
  // the status of the response
  grpc::ServerUnaryReactor* GetBids(grpc::CallbackServerContext* context,
                                    const GetBidsRequest* request,
                                    GetBidsResponse* response) override;

 private:
  // The Bidding signals provider is used to fetch signals required for bidding
  // from external sources, such as a KeyValue server or an HTTP server.
  std::unique_ptr<BiddingSignalsAsyncProvider> bidding_signals_async_provider_;
  // Config for modifying behaviour of GetBidsUnaryReactor.
  const GetBidsConfig config_;
  bool enable_benchmarking_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_;
  // Stub to make GRPC calls to the bidding service.
  std::unique_ptr<Bidding::Stub> stub_;
  // The BiddingAsyncClient is used to call Bidding Service to execute
  // AdTech's code in a secure privacy sandbox and generate the bids.
  // The bids received in response from the BiddingAsyncClient are returned
  // to the caller in the GetBid response.
  std::unique_ptr<BiddingAsyncClient> bidding_async_client_;
  std::unique_ptr<ProtectedAppSignalsBiddingAsyncClient>
      protected_app_signals_bidding_async_client_;
  std::unique_ptr<KVAsyncClient> kv_async_client_;
  server_common::Executor& executor_;
  RandomNumberGeneratorFactory rng_factory_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_H_
