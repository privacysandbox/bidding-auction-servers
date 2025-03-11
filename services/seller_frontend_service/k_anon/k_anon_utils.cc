//   Copyright 2024 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/seller_frontend_service/k_anon/k_anon_utils.h"

#include <memory>

#include "services/common/util/hash_util.h"
#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using AdScores =
    ::google::protobuf::RepeatedPtrField<ScoreAdsResponse::AdScore>;
using GhostWinnerForTopLevelAuction =
    AuctionResult::KAnonGhostWinner::GhostWinnerForTopLevelAuction;
using GhostWinnerPrivateAggregationSignals =
    AuctionResult::KAnonGhostWinner::GhostWinnerPrivateAggregationSignals;

GhostWinnerForTopLevelAuction GetGhostWinnerDataForTopLevelAuction(
    const ScoreAdsResponse::AdScore& score) {
  GhostWinnerForTopLevelAuction ghost_winner_for_top_level_auction;
  *ghost_winner_for_top_level_auction.mutable_ad_render_url() = score.render();
  for (const std::string& ad_component : score.component_renders()) {
    *ghost_winner_for_top_level_auction.add_ad_component_render_urls() =
        ad_component;
  }
  ghost_winner_for_top_level_auction.set_modified_bid(score.bid());
  if (!score.bid_currency().empty()) {
    *ghost_winner_for_top_level_auction.mutable_bid_currency() =
        score.bid_currency();
  }
  if (!score.ad_metadata().empty()) {
    *ghost_winner_for_top_level_auction.mutable_ad_metadata() =
        score.ad_metadata();
  }
  if (score.has_buyer_reporting_id()) {
    *ghost_winner_for_top_level_auction.mutable_buyer_reporting_id() =
        score.buyer_reporting_id();
  }
  if (score.has_buyer_and_seller_reporting_id()) {
    *ghost_winner_for_top_level_auction
         .mutable_buyer_and_seller_reporting_id() =
        score.buyer_and_seller_reporting_id();
  }
  if (score.has_selected_buyer_and_seller_reporting_id()) {
    *ghost_winner_for_top_level_auction
         .mutable_selected_buyer_and_seller_reporting_id() =
        score.selected_buyer_and_seller_reporting_id();
  }
  return ghost_winner_for_top_level_auction;
}

inline std::optional<std::string> GetStringIfReportingIDSet(
    bool has_field, const std::string& reporting_id) {
  if (has_field) {
    return reporting_id;
  }
  return std::nullopt;
}

void PopulateGhostWinnerPrivateAggregationSignals(
    const ScoreAdsResponse::AdScore& score,
    AuctionResult::KAnonGhostWinner& ghost_winner) {
  google::protobuf::RepeatedPtrField<PrivateAggregateReportingResponse>
      pagg_responses = score.top_level_contributions();
  for (const auto& response : pagg_responses) {
    if (response.adtech_origin() == score.interest_group_owner()) {
      for (const auto& contribution : response.contributions()) {
        if (contribution.event().event_type() == EventType::EVENT_TYPE_LOSS) {
          GhostWinnerPrivateAggregationSignals signals;
          signals.set_bucket(
              ConvertIntArrayToByteString(contribution.bucket()));
          signals.set_value(contribution.value().int_value());
          *ghost_winner.mutable_ghost_winner_private_aggregation_signals()
               ->Add() = std::move(signals);
          if (contribution.has_ig_idx()) {
            ghost_winner.set_interest_group_index(contribution.ig_idx());
          }
        }
      }
    } else if (ghost_winner.ghost_winner_private_aggregation_signals().size() >
               0) {
      break;
    }
  }
}

}  // namespace

KAnonJoinCandidate GetKAnonJoinCandidate(const ScoreAdsResponse::AdScore& score,
                                         const ReportWinMap& report_win_map) {
  HashUtil hash_util = HashUtil();
  KAnonJoinCandidate join_candidate;
  auto bidding_js_it = report_win_map.buyer_report_win_js_urls.find(
      score.interest_group_owner());
  DCHECK(bidding_js_it != report_win_map.buyer_report_win_js_urls.end());
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

KAnonAuctionResultData GetKAnonAuctionResultData(
    const std::optional<ScoreAdsResponse::AdScore>& high_score,
    const AdScores* ghost_winning_scores, bool is_component_auction,
    absl::AnyInvocable<KAnonJoinCandidate(const ScoreAdsResponse::AdScore&)>
        get_kanon_join_candidate,
    RequestLogContext& log_context) {
  // Set up k-anon winner's join candidate with hashes.
  std::unique_ptr<KAnonJoinCandidate> winner_join_candidate;
  if (high_score) {
    winner_join_candidate = std::make_unique<KAnonJoinCandidate>(
        get_kanon_join_candidate(*high_score));
    PS_VLOG(kNoisyInfo, log_context)
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
      ghost_winner.set_owner(score.interest_group_owner());
      ghost_winner.set_ig_name(score.interest_group_name());
      if (!score.top_level_contributions().empty()) {
        PopulateGhostWinnerPrivateAggregationSignals(score, ghost_winner);
      }

      if (is_component_auction) {
        *ghost_winner.mutable_ghost_winner_for_top_level_auction() =
            GetGhostWinnerDataForTopLevelAuction(score);
      }
      *ghost_winner.mutable_k_anon_join_candidates() =
          get_kanon_join_candidate(score);
      PS_VLOG(kNoisyInfo, log_context)
          << "Populated K-anon Ghost Winner at index " << i << ": "
          << ghost_winner.DebugString();
      kanon_ghost_winners->emplace_back(ghost_winner);
    }
  }

  return {.kanon_ghost_winners = std::move(kanon_ghost_winners),
          .kanon_winner_join_candidates = std::move(winner_join_candidate)};
}

KAnonAuctionResultData GetKAnonAuctionResultData(
    ScoreAdsResponse::AdScore* high_score, AdScores& ghost_winning_scores,
    RequestLogContext& log_context) {
  // Set up k-anon winner's join candidate with hashes.
  std::unique_ptr<KAnonJoinCandidate> winner_join_candidate;
  if (high_score && high_score->has_k_anon_join_candidate()) {
    winner_join_candidate = std::make_unique<KAnonJoinCandidate>(
        std::move(*high_score->mutable_k_anon_join_candidate()));
    PS_VLOG(kNoisyInfo, log_context)
        << "Populated K-anon Winner Join Candidate: "
        << winner_join_candidate->DebugString();
  }

  // Setup k-anon ghost winners.
  std::unique_ptr<KAnonGhostWinners> kanon_ghost_winners = nullptr;
  kanon_ghost_winners = std::make_unique<KAnonGhostWinners>(
      std::initializer_list<AuctionResult::KAnonGhostWinner>{});
  kanon_ghost_winners->reserve(ghost_winning_scores.size());
  for (int i = 0; i < ghost_winning_scores.size(); i++) {
    auto& score = ghost_winning_scores[i];
    AuctionResult::KAnonGhostWinner ghost_winner;
    // TODO (b/378149132): Map interest_group_index.
    ghost_winner.set_owner(score.interest_group_owner());
    ghost_winner.set_ig_name(score.interest_group_name());
    // TODO (b/377490502): Map ghost_winner_private_aggregation_signals.
    if (score.has_k_anon_join_candidate()) {
      *ghost_winner.mutable_k_anon_join_candidates() =
          std::move(*score.mutable_k_anon_join_candidate());
    }
    PS_VLOG(kNoisyInfo, log_context)
        << "Populated K-anon Ghost Winner at index " << i << ": "
        << ghost_winner.DebugString();
    kanon_ghost_winners->emplace_back(ghost_winner);
  }

  return {.kanon_ghost_winners = std::move(kanon_ghost_winners),
          .kanon_winner_join_candidates = std::move(winner_join_candidate)};
}

}  // namespace privacy_sandbox::bidding_auction_servers
