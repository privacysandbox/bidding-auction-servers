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

#ifndef SERVICES_AUCTION_SERVICE_AUCTION_SERVICE_H_
#define SERVICES_AUCTION_SERVICE_AUCTION_SERVICE_H_

#include <memory>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/functional/any_invocable.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/auction_service/data/runtime_config.h"
#include "services/auction_service/score_ads_reactor.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"
#include "src/public/cpio/interface/crypto_client/crypto_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

using ScoreAdsReactorFactory =
    absl::AnyInvocable<std::unique_ptr<ScoreAdsReactor>(
        grpc::CallbackServerContext* context, const ScoreAdsRequest* request,
        ScoreAdsResponse* response,
        server_common::KeyFetcherManagerInterface* key_fetcher_manager,
        CryptoClientWrapperInterface* crypto_client,
        const AuctionServiceRuntimeConfig& runtime_config)>;

// AuctionService implements business logic for running an auction
// that takes input from the SellerFrontEndService in the form of bids and
// signals and outputs scored ads.
// The auction uses proprietary AdTech code that runs in a secure privacy
// sandbox inside a secure Virtual Machine. The implementation dispatches
// to V8 to run the AdTech code.
class AuctionService final : public Auction::CallbackService {
 public:
  explicit AuctionService(
      ScoreAdsReactorFactory score_ads_reactor_factory,
      std::unique_ptr<server_common::KeyFetcherManagerInterface>
          key_fetcher_manager,
      std::unique_ptr<CryptoClientWrapperInterface> crypto_client,
      AuctionServiceRuntimeConfig runtime_config)
      : score_ads_reactor_factory_(std::move(score_ads_reactor_factory)),
        key_fetcher_manager_(std::move(key_fetcher_manager)),
        crypto_client_(std::move(crypto_client)),
        runtime_config_(std::move(runtime_config)) {}
  // Scores ads by running an ad auction.
  //
  // This is the API that is accessed by the SellerFrontEndService. This is the
  // rpc endpoint that will dispatch to V8 to execute the provided AdTech code.
  //
  // context: Standard gRPC-owned testing utility parameter.
  // request: Pointer to ScoreAdsRequest. The request is encrypted using
  // Hybrid Public Key Encryption (HPKE).
  // response: Pointer to ScoreAdsResponse. The response is encrypted
  // using HPKE.
  // return: a ServerUnaryReactor that is used by the gRPC library
  grpc::ServerUnaryReactor* ScoreAds(grpc::CallbackServerContext* context,
                                     const ScoreAdsRequest* request,
                                     ScoreAdsResponse* response) override;

 private:
  ScoreAdsReactorFactory score_ads_reactor_factory_;
  std::unique_ptr<server_common::KeyFetcherManagerInterface>
      key_fetcher_manager_;
  std::unique_ptr<CryptoClientWrapperInterface> crypto_client_;
  AuctionServiceRuntimeConfig runtime_config_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_AUCTION_SERVICE_H_
