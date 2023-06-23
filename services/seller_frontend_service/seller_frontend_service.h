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
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/clients/auction_server/scoring_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client_factory.h"
#include "services/common/reporters/async_reporter.h"
#include "services/seller_frontend_service/providers/scoring_signals_async_provider.h"
#include "services/seller_frontend_service/seller_frontend_config.pb.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

// This a utility class that acts as a wrapper for the clients that are used
// by SellerFrontEndService.
struct ClientRegistry {
  const ScoringSignalsAsyncProvider& scoring_signals_async_provider;
  const ScoringAsyncClient& scoring;
  const ClientFactory<BuyerFrontEndAsyncClient, absl::string_view>&
      buyer_factory;
  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  std::unique_ptr<AsyncReporter> reporting;
  explicit ClientRegistry(
      const ScoringSignalsAsyncProvider& scoring_signals_async_provider,
      const ScoringAsyncClient& scoring,
      const ClientFactory<BuyerFrontEndAsyncClient, absl::string_view>&
          buyer_factory,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      std::unique_ptr<AsyncReporter> reporting_)
      : scoring_signals_async_provider(scoring_signals_async_provider),
        scoring(scoring),
        buyer_factory(buyer_factory),
        key_fetcher_manager_(key_fetcher_manager),
        reporting(std::move(reporting_)) {
    CHECK(key_fetcher_manager_ != nullptr)
        << "KeyFetcherManager cannot be null";
  }
};

// SellerFrontEndService implements business logic to orchestrate requests
// to the Buyers participating in an ad auction. In addition, fetch the AdTech's
// proprietary code for scoring ads, looks up realtime seller's signals
// required for ad auction, and calls the Auction Service to execute AdTech's
// code in a secure privacy sandbox inside a secure Virtual Machine.
class SellerFrontEndService final : public SellerFrontEnd::CallbackService {
 public:
  explicit SellerFrontEndService(const SellerFrontEndConfig& config,
                                 const ClientRegistry& clients)
      : config_(config), clients_(clients) {}
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
  const SellerFrontEndConfig& config_;
  const ClientRegistry& clients_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELLER_FRONTEND_SERVICE_H_
