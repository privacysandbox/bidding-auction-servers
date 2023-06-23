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
#include "services/bidding_service/generate_bids_reactor.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr char kGenerateBids[] = "GenerateBids";

using GenerateBidsReactorFactory = absl::AnyInvocable<GenerateBidsReactor*(
    const GenerateBidsRequest* request, GenerateBidsResponse* response,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BiddingServiceRuntimeConfig& runtime_config)>;
// BiddingService implements business logic for generating a bid. It takes
// input from the BuyerFrontEndService.
// The bid generation uses proprietary AdTech code that runs in a secure privacy
// sandbox inside a secure Virtual Machine. The implementation dispatches to
// V8 to run the AdTech code.
class BiddingService final : public Bidding::CallbackService {
 public:
  explicit BiddingService(
      GenerateBidsReactorFactory generate_bids_reactor_factory,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      BiddingServiceRuntimeConfig runtime_config)
      : generate_bids_reactor_factory_(
            std::move(generate_bids_reactor_factory)),
        key_fetcher_manager_(key_fetcher_manager),
        crypto_client_(crypto_client),
        runtime_config_(std::move(runtime_config)) {}

  // Generate bids for ad candidates owned by the Buyer in an ad auction
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

 private:
  GenerateBidsReactorFactory generate_bids_reactor_factory_;
  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  CryptoClientWrapperInterface* crypto_client_;
  BiddingServiceRuntimeConfig runtime_config_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_BIDDING_SERVICE_H_
