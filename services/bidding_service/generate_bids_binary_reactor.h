//  Copyright 2024 Google LLC
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

#ifndef SERVICES_BIDDING_SERVICE_GENERATE_BIDS_BINARY_REACTOR_H_
#define SERVICES_BIDDING_SERVICE_GENERATE_BIDS_BINARY_REACTOR_H_

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.pb.h"
#include "api/udf/generate_bid_udf_interface.pb.h"
#include "services/bidding_service/base_generate_bids_reactor.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/utils/validation.h"
#include "services/common/clients/code_dispatcher/byob/byob_dispatch_client.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/async_task_tracker.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {

//  This is a gRPC reactor that serves a single GenerateBidsRequest by running
//  generateBid binary for each interest group in the raw request. It stores
//  state relevant to the request and after the response is finished being
//  served, GenerateBidsBinaryReactor cleans up all necessary state and grpc
//  releases the reactor from memory.
class GenerateBidsBinaryReactor
    : public BaseGenerateBidsReactor<
          GenerateBidsRequest, GenerateBidsRequest::GenerateBidsRawRequest,
          GenerateBidsResponse, GenerateBidsResponse::GenerateBidsRawResponse> {
 public:
  explicit GenerateBidsBinaryReactor(
      grpc::CallbackServerContext* context,
      ByobDispatchClient<roma_service::GenerateProtectedAudienceBidRequest,
                         roma_service::GenerateProtectedAudienceBidResponse>&
          byob_client,
      const GenerateBidsRequest* request, GenerateBidsResponse* response,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      server_common::Executor* executor,
      const BiddingServiceRuntimeConfig& runtime_config);

  // Initiates parallel synchronous executions for each interest group in the
  // GenerateBidsRequest using executor_.
  void Execute() override;

 private:
  // Cleans up and deletes the GenerateBidsBinaryReactor. Called by the grpc
  // library after the response has finished.
  void OnDone() override;

  // Initiates the synchronous execution for the interest group at the given
  // index of the GenerateBidsRequest.
  void ExecuteForInterestGroup(int ig_index);

  // Once all bids are fetched, this callback gets executed.
  void OnAllBidsDone(bool any_successful_bids);

  // Encrypts the response before the gRPC call is finished with the provided
  // status.
  void EncryptResponseAndFinish(grpc::Status status);

  grpc::CallbackServerContext* context_;

  // Dispatches execution requests to the ROMA BYOB library.
  ByobDispatchClient<roma_service::GenerateProtectedAudienceBidRequest,
                     roma_service::GenerateProtectedAudienceBidResponse>&
      byob_client_;

  // Keeps track of the pending bid requests and executes the registered
  // callback once all the bids have been fetched.
  AsyncTaskTracker async_task_tracker_;

  // The executor_ will receive execution tasks from Execute. It is not owned by
  // this class instance but is required to outlive the lifetime of this class
  // instance.
  server_common::Executor* executor_;

  // The maximum time a single ROMA execution will block for.
  absl::Duration roma_timeout_ms_duration_;

  // Stores the list of bids for an interest group, for each interest group.
  std::vector<std::vector<AdWithBid>> ads_with_bids_by_ig_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::BiddingContext> metric_context_;

  // Specifies whether this is a single seller or component auction.
  // Impacts the parsing of generateBid output.
  AuctionScope auction_scope_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_GENERATE_BIDS_BINARY_REACTOR_H_
