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

#ifndef SERVICES_BUYER_FRONTEND_SERVICE_GET_BIDS_UNARY_REACTOR_H_
#define SERVICES_BUYER_FRONTEND_SERVICE_GET_BIDS_UNARY_REACTOR_H_

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/blocking_counter.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/buyer_frontend_service/data/get_bids_config.h"
#include "services/buyer_frontend_service/providers/bidding_signals_async_provider.h"
#include "services/common/clients/bidding_server/bidding_async_client.h"
#include "services/common/encryption/crypto_client_wrapper_interface.h"
#include "services/common/loggers/benchmarking_logger.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/context_logger.h"
#include "src/cpp/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr std::array<std::pair<std::string_view, std::string_view>, 3>
    kBuyerKVMetadata = {{{"x-accept-language", "Accept-Language"},
                         {"x-user-agent", "User-Agent"},
                         {"x-bna-client-ip", "X-BnA-Client-IP"}}};

// This is a gRPC server reactor that serves a single GetBidsRequest.
// It stores state relevant to the request and after the
// response is finished being served, it cleans up all
// necessary state and grpc releases the reactor from memory.
class GetBidsUnaryReactor : public grpc::ServerUnaryReactor {
 public:
  explicit GetBidsUnaryReactor(
      grpc::CallbackServerContext& context,
      const GetBidsRequest& get_bids_request,
      GetBidsResponse& get_bids_response,
      const BiddingSignalsAsyncProvider& bidding_signals_async_provider,
      const BiddingAsyncClient& bidding_async_client,
      const GetBidsConfig& config,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      bool enable_benchmarking = false);

  // GetBidsUnaryReactor is neither copyable nor movable.
  GetBidsUnaryReactor(const GetBidsUnaryReactor&) = delete;
  GetBidsUnaryReactor& operator=(const GetBidsUnaryReactor&) = delete;

  // Starts the execution the request.
  void Execute();
  // Runs once the request has finished execution and deletes current instance.
  void OnDone() override;
  // Runs if the request is cancelled in the middle of execution.
  void OnCancel() override {
    // TODO(b/249183477): Figure out how to cancel request
  }

 private:
  // Process Outputs from Actions to prepare bidding request.
  // All Preload actions must have completed before this is invoked.
  void PrepareAndGenerateProtectedAudienceBid(
      std::unique_ptr<BiddingSignals> bidding_signals);

  // Decrypts the request ciphertext in and returns whether decryption was
  // successful. If successful, the result is written into 'raw_request_'.
  bool DecryptRequest();

  // Encrypts `raw_response` and sets the result on the 'response_ciphertext'
  // field in the response. Returns whether encryption was successful.
  bool EncryptResponse();

  // Gets logging context (as a key/val pair) that can help debug/trace a
  // request through the BA services.
  ContextLogger::ContextMap GetLoggingContext();

  // Finishes the RPC call with an OK status.
  void FinishWithOkStatus();

  // References for state, request, response and context from gRPC.
  // Should be released by gRPC
  // https://github.com/grpc/grpc/blob/dbc45208e2bfe14f01b1cbb06d0cd7c01077debb/include/grpcpp/server_context.h#L604
  grpc::CallbackServerContext* context_;
  const GetBidsRequest* request_;
  GetBidsRequest::GetBidsRawRequest raw_request_;

  // Should be released by gRPC after call is finished
  GetBidsResponse* get_bids_response_;
  std::unique_ptr<GetBidsResponse::GetBidsRawResponse> get_bids_raw_response_ =
      nullptr;

  // Metadata to be sent to buyer KV server.
  RequestMetadata kv_metadata_;

  // Helper classes for performing preload actions.
  // These are not owned by this class.
  const BiddingSignalsAsyncProvider* bidding_signals_async_provider_;
  const BiddingAsyncClient* bidding_async_client_;
  const GetBidsConfig& config_;
  server_common::KeyFetcherManagerInterface* key_fetcher_manager_;
  CryptoClientWrapperInterface* crypto_client_;
  std::unique_ptr<BenchmarkingLogger> benchmarking_logger_;
  std::string hpke_secret_;
  ContextLogger logger_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::BfeContext> metric_context_;

  // Gets Protected Audience Bids.
  void GetProtectedAudienceBids();
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BUYER_FRONTEND_SERVICE_GET_BIDS_UNARY_REACTOR_H_
