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

#ifndef FLEDGE_SERVICES_COMMON_CLIENTS_SCORING_ASYNC_CLIENT_H_
#define FLEDGE_SERVICES_COMMON_CLIENTS_SCORING_ASYNC_CLIENT_H_

#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/functional/any_invocable.h"
#include "absl/time/time.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/clients/async_grpc/default_async_grpc_client.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
using ScoringAsyncClient = AsyncClient<ScoreAdsRequest, ScoreAdsResponse,
                                       ScoreAdsRequest::ScoreAdsRawRequest,
                                       ScoreAdsResponse::ScoreAdsRawResponse>;

struct AuctionServiceClientConfig {
  std::string server_addr;
  bool compression = false;
  bool secure_client = true;
  std::string grpc_arg_default_authority = "";
  std::string ca_root_pem = "/etc/ssl/certs/ca-certificates.crt";
};

// This class is an async grpc client for the Fledge Auction (Scoring) Service.
class ScoringAsyncGrpcClient
    : public DefaultAsyncGrpcClient<ScoreAdsRequest, ScoreAdsResponse,
                                    ScoreAdsRequest::ScoreAdsRawRequest,
                                    ScoreAdsResponse::ScoreAdsRawResponse> {
 public:
  explicit ScoringAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const AuctionServiceClientConfig& client_config);

 protected:
  // Sends an asynchronous request via grpc to the Scoring Service.
  //
  // params: a pointer to the ClientParams object which carries data used
  // by the grpc stub.
  void SendRpc(const std::string& hpke_secret, grpc::ClientContext* context,
               RawClientParams<ScoreAdsRequest, ScoreAdsResponse,
                               ScoreAdsResponse::ScoreAdsRawResponse>* params)
      const override;

  std::unique_ptr<Auction::Stub> stub_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_CLIENTS_SCORING_ASYNC_CLIENT_H_
