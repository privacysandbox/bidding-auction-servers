/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_BIDDING_SERVICE_PROTECTED_APP_SIGNALS_GENERATE_BIDS_BINARY_REACTOR_H_
#define SERVICES_BIDDING_SERVICE_PROTECTED_APP_SIGNALS_GENERATE_BIDS_BINARY_REACTOR_H_

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/data/runtime_config.h"

namespace privacy_sandbox::bidding_auction_servers {

//  This is a barebones gRPC reactor that handles incoming
//  GenerateProtectedAppSignalsBidsRequests for a BYOB-enabled server by
//  replying with an Unimplemented error.
class ProtectedAppSignalsGenerateBidsBinaryReactor
    : public BaseGenerateBidsReactor<
          GenerateProtectedAppSignalsBidsRequest,
          GenerateProtectedAppSignalsBidsRequest::
              GenerateProtectedAppSignalsBidsRawRequest,
          GenerateProtectedAppSignalsBidsResponse,
          GenerateProtectedAppSignalsBidsResponse::
              GenerateProtectedAppSignalsBidsRawResponse> {
 public:
  explicit ProtectedAppSignalsGenerateBidsBinaryReactor(
      const BiddingServiceRuntimeConfig& runtime_config,
      const GenerateProtectedAppSignalsBidsRequest* request,
      GenerateProtectedAppSignalsBidsResponse* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client);

  // Executes the workflow required to handle the incoming GRPC request.
  void Execute() override;

 private:
  void OnDone() override;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_PROTECTED_APP_SIGNALS_GENERATE_BIDS_BINARY_REACTOR_H_
