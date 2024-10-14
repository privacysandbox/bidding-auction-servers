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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AUCTION_RESULT_REACTOR_WEB_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AUCTION_RESULT_REACTOR_WEB_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "api/bidding_auction_servers.grpc.pb.h"
#include "api/bidding_auction_servers.pb.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/feature_flags.h"
#include "services/common/metric/server_definition.h"
#include "services/common/util/client_contexts.h"
#include "services/common/util/error_accumulator.h"
#include "services/seller_frontend_service/seller_frontend_service.h"
#include "services/seller_frontend_service/util/encryption_util.h"
#include "services/seller_frontend_service/util/proto_mapping_util.h"
#include "services/seller_frontend_service/util/web_utils.h"

namespace privacy_sandbox::bidding_auction_servers {
// Marker to set state of request in metric context.
inline constexpr absl::string_view kWinningAuctionAd = "winning_auction_ad";

// This is a gRPC reactor that serves a single SelectAdRequest for a top
// level auction, which involves AuctionResults from other sellers.
// It stores state relevant to the request and after the
// response is finished being served, SelectAuctionResultReactor cleans up all
// necessary state and grpc releases the reactor from memory.
class SelectAuctionResultReactor : public grpc::ServerUnaryReactor {
 public:
  explicit SelectAuctionResultReactor(
      grpc::CallbackServerContext* context, const SelectAdRequest* request,
      SelectAdResponse* response, const ClientRegistry& clients,
      const TrustedServersConfigClient& config_client);
  virtual ~SelectAuctionResultReactor() = default;

  // SelectAuctionResultReactor is neither copyable nor movable.
  SelectAuctionResultReactor(const SelectAuctionResultReactor&) = delete;
  SelectAuctionResultReactor& operator=(const SelectAuctionResultReactor&) =
      delete;

  // Initiate the asynchronous execution of the SelectAdRequest.
  void Execute();

  // Cleans up and deletes the SelectAuctionResultReactor. Called by the grpc
  // library after the response has finished.
  void OnDone() override;

  // Abandons the entire SelectAuctionResultReactor. Called by the grpc library
  // if the client cancels the request.
  void OnCancel() override;

 private:
  // Initialization
  grpc::CallbackServerContext* request_context_;
  const SelectAdRequest* request_;
  SelectAdResponse* response_;
  std::variant<ProtectedAudienceInput, ProtectedAuctionInput>
      protected_auction_input_;
  absl::string_view seller_domain_;
  absl::string_view request_generation_id_;
  bool is_protected_auction_request_;
  std::vector<IgsWithBidsMap> component_auction_bidding_groups_;
  UpdateGroupMap component_auction_update_groups_;
  const ClientRegistry& clients_;
  const TrustedServersConfigClient& config_client_;

  RequestLogContext log_context_;

  // Used to log metric, same life time as reactor.
  std::unique_ptr<metric::SfeContext> metric_context_;

  // Object that accumulates all the errors and aggregates them based on their
  // intended visibility.
  ErrorAccumulator error_accumulator_;

  const bool enable_cancellation_;

  // Encryption context needed throughout the lifecycle of the request.
  std::unique_ptr<OhttpHpkeDecryptedMessage> decrypted_request_;

  // Start time of request.
  absl::Time start_ = absl::Now();

  // Keeps track of the client contexts used for RPC calls
  ClientContexts client_contexts_;

  // Finishes the RPC call with a status and publishes metrics.
  void FinishWithStatus(const grpc::Status& status);

  // Logs metrics from request size.
  void LogRequestMetrics();

  // Called to start the score ads RPC.
  // This function moves the elements from component_auction_results and
  // signals fields from auction_config and protected_auction_input.
  // These fields should not be used after this function has been called.
  void ScoreAds(std::vector<AuctionResult>& component_auction_results);

  // Called after score ads RPC is complete to collate the result.
  void OnScoreAdsDone(
      absl::StatusOr<std::unique_ptr<ScoreAdsResponse::ScoreAdsRawResponse>>
          response);

  // Move response object to this method.
  void FinishWithResponse(
      absl::StatusOr<std::string> auction_result_ciphertext);

  void FinishWithClientVisibleErrors(absl::string_view message);

  void FinishWithServerVisibleErrors(absl::string_view message);

  // Sets logging context from decrypted Protected Audience Input.
  // Will use the empty protected audience object if this is not
  // populated from the request.
  void SetLoggingContextWithProtectedAuctionInput();
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AUCTION_RESULT_REACTOR_WEB_H_
