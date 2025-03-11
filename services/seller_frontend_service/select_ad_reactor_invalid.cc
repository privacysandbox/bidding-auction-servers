// Copyright 2023 Google LLC
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

#include "services/seller_frontend_service/select_ad_reactor_invalid.h"

#include <grpcpp/grpcpp.h>

namespace privacy_sandbox::bidding_auction_servers {

SelectAdReactorInvalid::SelectAdReactorInvalid(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, server_common::Executor* executor,
    const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client,
    const ReportWinMap& report_win_map)
    : SelectAdReactor(context, request, response, executor, clients,
                      config_client, report_win_map),
      client_type_(request->client_type()) {}

void SelectAdReactorInvalid::Execute() {
  PS_VLOG(5, SystemLogContext())
      << "SelectAdRequest received:\n"
      << request_->DebugString()
      << ", request size: " << request_->ByteSizeLong();
  // This reactor is invoked in only following two cases: 1) When request is
  // empty. 2) When the client type is not set appropriately.
  if (request_->ByteSizeLong() == 0) {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kEmptySelectAdRequest,
                ErrorCode::CLIENT_SIDE);
  } else {
    ReportError(ErrorVisibility::AD_SERVER_VISIBLE, kUnsupportedClientType,
                ErrorCode::CLIENT_SIDE);
  }
  LogIfError(
      metric_context_->AccumulateMetric<metric::kSfeErrorCountByErrorCode>(
          1, metric::kSfeSelectAdRequestBadInput));
  PS_LOG(WARNING, SystemLogContext())
      << "Finishing the SelectAdRequest RPC with ad server visible error";

  FinishWithStatus(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                error_accumulator_.GetAccumulatedErrorString(
                                    ErrorVisibility::AD_SERVER_VISIBLE)));
}

absl::StatusOr<std::string> SelectAdReactorInvalid::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error,
    const AdScores* ghost_winning_scores,
    int per_adtech_paapi_contributions_limit) {
  return "";
}

ProtectedAudienceInput SelectAdReactorInvalid::GetDecodedProtectedAudienceInput(
    absl::string_view encoded_data) {
  return {};
}

ProtectedAuctionInput SelectAdReactorInvalid::GetDecodedProtectedAuctionInput(
    absl::string_view encoded_data) {
  return {};
}

absl::flat_hash_map<absl::string_view, BuyerInputForBidding>
SelectAdReactorInvalid::GetDecodedBuyerinputs(
    const google::protobuf::Map<std::string, std::string>&
        encoded_buyer_inputs) {
  return {};
}

KAnonJoinCandidate SelectAdReactorInvalid::GetKAnonJoinCandidate(
    const ScoreAdsResponse::AdScore& score) {
  return {};
}

absl::string_view SelectAdReactorInvalid::GetKAnonSetType() { return ""; }

}  // namespace privacy_sandbox::bidding_auction_servers
