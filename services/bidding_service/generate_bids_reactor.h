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

#ifndef SERVICES_BIDDING_SERVICE_GENERATE_BIDS_REACTOR_H_
#define SERVICES_BIDDING_SERVICE_GENERATE_BIDS_REACTOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/benchmarking/bidding_benchmarking_logger.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/common/clients/code_dispatcher/v8_dispatch_client.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/metric/server_definition.h"

namespace privacy_sandbox::bidding_auction_servers {

// See GenerateBidInput for more detail on each field.
enum class GenerateBidArgs : int {
  kInterestGroup = 0,
  kAuctionSignals,
  kBuyerSignals,
  kTrustedBiddingSignals,
  kDeviceSignals,
  kFeatureFlags
};

//  This is a gRPC reactor that serves a single GenerateBidsRequest.
//  It stores state relevant to the request and after the
//  response is finished being served, GenerateBidsReactor cleans up all
//  necessary state and grpc releases the reactor from memory.
class GenerateBidsReactor
    : public BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse> {
 public:
  explicit GenerateBidsReactor(
      grpc::CallbackServerContext* context, V8DispatchClient& dispatcher,
      const GenerateBidsRequest* request, GenerateBidsResponse* response,
      std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const BiddingServiceRuntimeConfig& runtime_config);

  // Initiates the asynchronous execution of the GenerateBidsRequest.
  void Execute() override;

 private:
  // Cleans up and deletes the GenerateBidsReactor. Called by the grpc library
  // after the response has finished.
  void OnDone() override;

  // Asynchronous callback used by the v8 code executor to return a result. This
  // will be called in a different thread owned by the code dispatch library.
  //
  // output: a status or DispatchResponse representing the result of the code
  // dispatch execution.
  // interest_group_name: the name of the interest group that issued the
  // code dispatch request.
  void GenerateBidsCallback(
      const std::vector<absl::StatusOr<DispatchResponse>>& output);

  // Encrypts the response before the GRPC call is finished with the provided
  // status.
  void EncryptResponseAndFinish(grpc::Status status);

  grpc::CallbackServerContext* context_;

  // Dispatches execution requests to a library that runs V8 workers in
  // separate processes.
  V8DispatchClient& dispatcher_;
  std::vector<DispatchRequest> dispatch_requests_;

  std::unique_ptr<BiddingBenchmarkingLogger> benchmarking_logger_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::BiddingContext> metric_context_;

  // Specifies whether this is a single seller or component auction.
  // Impacts the parsing of generateBid output.
  AuctionScope auction_scope_;

  // UDF version to use for this request.
  const std::string& protected_auction_generate_bid_version_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_GENERATE_BIDS_REACTOR_H_
