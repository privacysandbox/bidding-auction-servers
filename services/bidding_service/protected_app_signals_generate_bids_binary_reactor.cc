// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/protected_app_signals_generate_bids_binary_reactor.h"

namespace privacy_sandbox::bidding_auction_servers {

ProtectedAppSignalsGenerateBidsBinaryReactor::
    ProtectedAppSignalsGenerateBidsBinaryReactor(
        const BiddingServiceRuntimeConfig& runtime_config,
        const GenerateProtectedAppSignalsBidsRequest* request,
        GenerateProtectedAppSignalsBidsResponse* response,
        server_common::KeyFetcherManagerInterface* key_fetcher_manager,
        CryptoClientWrapperInterface* crypto_client)
    : BaseGenerateBidsReactor<GenerateProtectedAppSignalsBidsRequest,
                              GenerateProtectedAppSignalsBidsRequest::
                                  GenerateProtectedAppSignalsBidsRawRequest,
                              GenerateProtectedAppSignalsBidsResponse,
                              GenerateProtectedAppSignalsBidsResponse::
                                  GenerateProtectedAppSignalsBidsRawResponse>(
          runtime_config, request, response, key_fetcher_manager,
          crypto_client) {}

void ProtectedAppSignalsGenerateBidsBinaryReactor::Execute() {
  // BYOB does not support PAS. Finish execution with Unimplemented error.
  Finish(grpc::Status(grpc::StatusCode::UNIMPLEMENTED,
                      "PAS is currently not supported with BYOB."));
  return;
}

void ProtectedAppSignalsGenerateBidsBinaryReactor::OnDone() { delete this; }

}  // namespace privacy_sandbox::bidding_auction_servers
