/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_APP_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_APP_H_

#include <memory>
#include <optional>
#include <string>

#include <grpcpp/grpcpp.h>

#include "absl/status/statusor.h"
#include "include/grpcpp/impl/codegen/server_callback.h"
#include "services/seller_frontend_service/select_ad_reactor.h"

namespace privacy_sandbox::bidding_auction_servers {
class SelectAdReactorForApp : public SelectAdReactor {
 public:
  explicit SelectAdReactorForApp(
      grpc::CallbackServerContext* context, const SelectAdRequest* request,
      SelectAdResponse* response, const ClientRegistry& clients,
      const TrustedServersConfigClient& config_client, bool fail_fast = true);
  virtual ~SelectAdReactorForApp() = default;

  // SelectAdReactorForApp is neither copyable nor movable.
  SelectAdReactorForApp(const SelectAdReactorForApp&) = delete;
  SelectAdReactorForApp& operator=(const SelectAdReactorForApp&) = delete;

 private:
  absl::StatusOr<std::string> GetNonEncryptedResponse(
      const std::optional<ScoreAdsResponse::AdScore>& high_score,
      const std::optional<AuctionResult::Error>& error) override;

  [[deprecated]] ProtectedAudienceInput GetDecodedProtectedAudienceInput(
      absl::string_view encoded_data) override;

  ProtectedAuctionInput GetDecodedProtectedAuctionInput(
      absl::string_view encoded_data) override;

  absl::flat_hash_map<absl::string_view, BuyerInput> GetDecodedBuyerinputs(
      const google::protobuf::Map<std::string, std::string>&
          encoded_buyer_inputs) override;

  // Protected App Signals (PAS) related methods follow.

  // PAS buyer input for the GetBid Request to be sent to BFE. The buyer input
  // from client may carry both Protected Audience (PA) and Protected App
  // Signals (PAS) buyer inputs and this method forks those out while making the
  // outbound call.
  void MayPopulateProtectedAppSignalsBuyerInput(
      absl::string_view buyer,
      GetBidsRequest::GetBidsRawRequest* get_bids_raw_request);

  // Creates a GetBids request to be sent to BFE. If PAS is enabled and client
  // specified PAS, then the created GetBid request will have separate PA and
  // PAS buyer inputs populated properly.
  std::unique_ptr<GetBidsRequest::GetBidsRawRequest> CreateGetBidsRequest(
      const std::string& buyer_ig_owner,
      const BuyerInput& buyer_input) override;

  // Populates PAS bids in the scoring request to be sent to auction service.
  void MayPopulateProtectedAppSignalsBids(
      ScoreAdsRequest::ScoreAdsRawRequest* score_ads_raw_request);

  ScoreAdsRequest::ScoreAdsRawRequest::ProtectedAppSignalsAdWithBidMetadata
  BuildProtectedAppSignalsAdWithBidMetadata(
      absl::string_view buyer, const ProtectedAppSignalsAdWithBid& input,
      bool k_anon_status);

  std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest> CreateScoreAdsRequest()
      override;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_SELECT_AD_REACTOR_APP_H_
