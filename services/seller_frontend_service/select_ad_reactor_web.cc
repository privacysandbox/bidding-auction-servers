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

#include "services/seller_frontend_service/select_ad_reactor_web.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/strings/str_format.h"
#include "services/common/compression/gzip.h"
#include "services/common/util/hash_util.h"
#include "services/common/util/request_response_constants.h"
#include "services/seller_frontend_service/util/framing_utils.h"
#include "services/seller_frontend_service/util/proto_mapping_util.h"
#include "services/seller_frontend_service/util/web_utils.h"
#include "src/communication/encoding_utils.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::google::protobuf::RepeatedPtrField;
using BiddingGroupsMap =
    ::google::protobuf::Map<std::string, AuctionResult::InterestGroupIndex>;
using EncodedBuyerInputs = ::google::protobuf::Map<std::string, std::string>;
using DecodedBuyerInputs = absl::flat_hash_map<absl::string_view, BuyerInput>;
using ReportErrorSignature = std::function<void(
    log::ParamWithSourceLoc<ErrorVisibility> error_visibility_with_loc,
    const std::string& msg, ErrorCode error_code)>;

namespace {

template <typename T>
T GetDecodedProtectedAuctionInputHelper(absl::string_view encoded_data,
                                        bool fail_fast,
                                        const ReportErrorSignature& ReportError,
                                        ErrorAccumulator& error_accumulator) {
  absl::StatusOr<server_common::DecodedRequest> decoded_request =
      server_common::DecodeRequestPayload(encoded_data);
  if (!decoded_request.ok()) {
    ReportError(ErrorVisibility::CLIENT_VISIBLE,
                std::string(decoded_request.status().message()),
                ErrorCode::CLIENT_SIDE);
    return T{};
  }
  std::string payload = std::move(decoded_request->compressed_data);
  return Decode<T>(payload, error_accumulator, fail_fast);
}

inline std::optional<std::string> GetStringIfReportingIDSet(
    bool has_field, const std::string& reporting_id) {
  if (has_field) {
    return reporting_id;
  }
  return std::nullopt;
}

}  // namespace

SelectAdReactorForWeb::SelectAdReactorForWeb(
    grpc::CallbackServerContext* context, const SelectAdRequest* request,
    SelectAdResponse* response, const ClientRegistry& clients,
    const TrustedServersConfigClient& config_client,
    const ReportWinMap& report_win_map, bool enable_cancellation,
    bool enable_kanon, bool fail_fast, int max_buyers_solicited)
    : SelectAdReactor(context, request, response, clients, config_client,
                      report_win_map, enable_cancellation, enable_kanon,
                      fail_fast, max_buyers_solicited) {}

AuctionResult::KAnonJoinCandidate SelectAdReactorForWeb::GetKAnonJoinCandidate(
    const ScoreAdsResponse::AdScore& score) {
  HashUtil hash_util = HashUtil();
  AuctionResult::KAnonJoinCandidate join_candidate;
  auto bidding_js_it = report_win_map_.buyer_report_win_js_urls.find(
      score.interest_group_owner());
  DCHECK(bidding_js_it != report_win_map_.buyer_report_win_js_urls.end());
  join_candidate.set_ad_render_url_hash(hash_util.HashedKAnonKeyForAdRenderURL(
      score.interest_group_owner(), bidding_js_it->second, score.render()));
  auto* ghost_ad_component_render_urls_hash =
      join_candidate.mutable_ad_component_render_urls_hash();
  for (int i = 0; i < score.component_renders().size(); i++) {
    *ghost_ad_component_render_urls_hash->Add() =
        hash_util.HashedKAnonKeyForAdComponentRenderURL(
            score.component_renders()[i]);
  }
  KAnonKeyReportingIDParam score_ids = {
      .buyer_reporting_id = GetStringIfReportingIDSet(
          score.has_buyer_reporting_id(), score.buyer_reporting_id()),
      .buyer_and_seller_reporting_id =
          GetStringIfReportingIDSet(score.has_buyer_and_seller_reporting_id(),
                                    score.buyer_and_seller_reporting_id())};
  join_candidate.set_reporting_id_hash(hash_util.HashedKAnonKeyForReportingID(
      score.interest_group_owner(), score.interest_group_name(),
      bidding_js_it->second, score.render(), score_ids));
  return join_candidate;
}

KAnonAuctionResultData SelectAdReactorForWeb::GetKAnonAuctionResultData(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const AdScores* ghost_winning_scores) {
  // Set up k-anon winner's join candidate with hashes.
  std::unique_ptr<KAnonJoinCandidate> winner_join_candidate;
  if (high_score) {
    winner_join_candidate = std::make_unique<KAnonJoinCandidate>(
        GetKAnonJoinCandidate(*high_score));
    PS_VLOG(kNoisyInfo, log_context_)
        << "Populated K-anon Winner Join Candidate: "
        << winner_join_candidate->DebugString();
  }

  // Setup k-anon ghost winners.
  std::unique_ptr<KAnonGhostWinners> kanon_ghost_winners = nullptr;
  if (ghost_winning_scores != nullptr) {
    kanon_ghost_winners = std::make_unique<KAnonGhostWinners>(
        std::initializer_list<AuctionResult::KAnonGhostWinner>{});
    kanon_ghost_winners->reserve(ghost_winning_scores->size());
    for (int i = 0; i < ghost_winning_scores->size(); i++) {
      const auto& score = (*ghost_winning_scores)[i];
      AuctionResult::KAnonGhostWinner ghost_winner;
      // TODO (b/378149132): Map interest_group_index.
      ghost_winner.set_owner(score.interest_group_owner());
      ghost_winner.set_ig_name(score.interest_group_name());
      // TODO (b/377490502): Map ghost_winner_private_aggregation_signals.
      // TODO (b/378149800): Map top_level fields when we start supporting
      // multi-seller auctions.
      *ghost_winner.mutable_k_anon_join_candidates() =
          GetKAnonJoinCandidate(score);
      PS_VLOG(kNoisyInfo, log_context_)
          << "Populated K-anon Ghost Winner at index " << i << ": "
          << ghost_winner.DebugString();
      kanon_ghost_winners->emplace_back(ghost_winner);
    }
  }

  return {.kanon_ghost_winners = std::move(kanon_ghost_winners),
          .kanon_winner_join_candidates = std::move(winner_join_candidate)};
}

absl::StatusOr<std::string> SelectAdReactorForWeb::GetNonEncryptedResponse(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const std::optional<AuctionResult::Error>& error,
    const AdScores* ghost_winning_scores) {
  auto error_handler =
      absl::bind_front(&SelectAdReactorForWeb::FinishWithStatus, this);
  std::string encoded_data;
  const auto decode_lambda = [&encoded_data, this]() {
    auto result = CborDecodeAuctionResultToProto(encoded_data);
    if (result.ok()) {
      log_context_.SetEventMessageField(*result);
      return std::string("exported in EventMessage if consented");
    } else {
      return result.status().ToString();
    }
  };

  if (auction_scope_ ==
      AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER) {
    PS_ASSIGN_OR_RETURN(
        encoded_data,
        EncodeComponent(
            request_->auction_config().top_level_seller(), high_score,
            GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
            shared_ig_updates_map_, error, error_handler));
    PS_VLOG(kPlain, log_context_) << "AuctionResult: " << (decode_lambda());
  } else if (auction_scope_ ==
             AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER) {
    // If this is server component auction, serialize as proto.
    AuctionResult auction_result;
    if (high_score) {
      auction_result = AdScoreToAuctionResult(
          high_score, GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
          shared_ig_updates_map_, error, auction_scope_,
          request_->auction_config().seller(), protected_auction_input_,
          request_->auction_config().top_level_seller());
    } else {
      auction_result = AdScoreToAuctionResult(
          high_score, std::nullopt, shared_ig_updates_map_, error,
          auction_scope_, request_->auction_config().seller(),
          protected_auction_input_);
    }
    // Serialized the data to bytes array.
    encoded_data = auction_result.SerializeAsString();

    if (server_common::log::PS_VLOG_IS_ON(kPlain)) {
      PS_VLOG(kPlain, log_context_)
          << "AuctionResult exported in EventMessage if consented";
      log_context_.SetEventMessageField(auction_result);
    }
  } else {
    // SINGLE_SELLER or SERVER_TOP_LEVEL Auction
    std::optional<KAnonAuctionResultData> kanon_data = std::nullopt;
    if (enable_enforce_kanon_) {
      kanon_data = GetKAnonAuctionResultData(high_score, ghost_winning_scores);
    }
    PS_ASSIGN_OR_RETURN(
        encoded_data,
        Encode(high_score,
               GetBiddingGroups(shared_buyer_bids_map_, *buyer_inputs_),
               shared_ig_updates_map_, error, error_handler, kanon_data));

    PS_VLOG(kPlain, log_context_) << "AuctionResult:\n" << (decode_lambda());
  }

  absl::string_view data_to_compress = absl::string_view(
      reinterpret_cast<char*>(encoded_data.data()), encoded_data.size());

  absl::StatusOr<std::string> compressed_data = GzipCompress(data_to_compress);
  if (!compressed_data.ok()) {
    PS_LOG(ERROR, log_context_)
        << "Failed to compress the CBOR serialized data: "
        << compressed_data.status().message();
    return absl::InternalError("");
  }

  return server_common::EncodeResponsePayload(
      server_common::CompressionType::kGzip, *compressed_data,
      GetEncodedDataSize(compressed_data->size()));
}

ProtectedAudienceInput SelectAdReactorForWeb::GetDecodedProtectedAudienceInput(
    absl::string_view encoded_data) {
  return GetDecodedProtectedAuctionInputHelper<ProtectedAudienceInput>(
      encoded_data, fail_fast_,
      std::bind(&SelectAdReactorForWeb::ReportError, this,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3),
      error_accumulator_);
}

ProtectedAuctionInput SelectAdReactorForWeb::GetDecodedProtectedAuctionInput(
    absl::string_view encoded_data) {
  return GetDecodedProtectedAuctionInputHelper<ProtectedAuctionInput>(
      encoded_data, fail_fast_,
      std::bind(&SelectAdReactorForWeb::ReportError, this,
                std::placeholders::_1, std::placeholders::_2,
                std::placeholders::_3),
      error_accumulator_);
}

DecodedBuyerInputs SelectAdReactorForWeb::GetDecodedBuyerinputs(
    const EncodedBuyerInputs& encoded_buyer_inputs) {
  return DecodeBuyerInputs(encoded_buyer_inputs, error_accumulator_,
                           fail_fast_);
}

}  // namespace privacy_sandbox::bidding_auction_servers
