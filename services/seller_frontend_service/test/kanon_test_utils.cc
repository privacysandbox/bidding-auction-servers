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

#include "services/seller_frontend_service/test/kanon_test_utils.h"

#include <utility>

namespace privacy_sandbox::bidding_auction_servers {
namespace {

struct KAnonJoinCandidateInputs {
  std::vector<uint8_t> ad_render_url_hash;
  std::vector<uint8_t> ad_component_render_urls_hash;
  std::vector<uint8_t> reporting_id_hash;
};

KAnonJoinCandidate SampleKAnonJoinCandidate(KAnonJoinCandidateInputs inputs) {
  KAnonJoinCandidate kanon_winner_join_candidates;
  auto& ad_render_url_hash = inputs.ad_render_url_hash;
  kanon_winner_join_candidates.set_ad_render_url_hash(
      std::string(std::make_move_iterator(ad_render_url_hash.begin()),
                  std::make_move_iterator(ad_render_url_hash.end())));
  auto* ad_component_render_urls_hash =
      kanon_winner_join_candidates.mutable_ad_component_render_urls_hash();
  auto& ad_component_render_urls_hash_val =
      inputs.ad_component_render_urls_hash;
  *ad_component_render_urls_hash->Add() = std::string(
      std::make_move_iterator(ad_component_render_urls_hash_val.begin()),
      std::make_move_iterator(ad_component_render_urls_hash_val.end()));
  auto& reporting_id_hash = inputs.reporting_id_hash;
  kanon_winner_join_candidates.set_reporting_id_hash(
      std::string(std::make_move_iterator(reporting_id_hash.begin()),
                  std::make_move_iterator(reporting_id_hash.end())));
  return kanon_winner_join_candidates;
}

}  // namespace

std::unique_ptr<KAnonAuctionResultData> SampleKAnonAuctionResultData(
    KAnonAuctionResultDataInputs inputs) {
  // Setup k-anon ghost winners.
  AuctionResult::KAnonGhostWinner kanon_ghost_winner;
  auto& ad_render_url_hash = inputs.ad_render_url_hash;
  auto& ad_component_render_urls_hash = inputs.ad_component_render_urls_hash;
  auto& reporting_id_hash = inputs.reporting_id_hash;
  auto k_anon_join_candidate = SampleKAnonJoinCandidate(
      {.ad_render_url_hash = std::vector<uint8_t>(
           std::make_move_iterator(ad_render_url_hash.begin()),
           std::make_move_iterator(ad_render_url_hash.end())),
       .ad_component_render_urls_hash = std::vector<uint8_t>(
           std::make_move_iterator(ad_component_render_urls_hash.begin()),
           std::make_move_iterator(ad_component_render_urls_hash.end())),
       .reporting_id_hash = std::vector<uint8_t>(
           std::make_move_iterator(reporting_id_hash.begin()),
           std::make_move_iterator(reporting_id_hash.end()))});
  *kanon_ghost_winner.mutable_k_anon_join_candidates() = k_anon_join_candidate;
  kanon_ghost_winner.set_interest_group_index(inputs.ig_index);
  kanon_ghost_winner.set_owner(std::move(inputs.ig_owner));
  kanon_ghost_winner.set_ig_name(std::move(inputs.ig_name));
  // Add private aggregation signals.
  auto* ghost_winner_private_aggregation_signals =
      kanon_ghost_winner.mutable_ghost_winner_private_aggregation_signals();
  AuctionResult::KAnonGhostWinner::GhostWinnerPrivateAggregationSignals signal;
  signal.set_bucket(
      std::string(std::make_move_iterator(inputs.bucket_name.begin()),
                  std::make_move_iterator(inputs.bucket_name.end())));
  signal.set_value(inputs.bucket_value);
  *ghost_winner_private_aggregation_signals->Add() = std::move(signal);
  // Add ghost winner for top level auction.
  auto* ghost_winner_for_top_level_auction =
      kanon_ghost_winner.mutable_ghost_winner_for_top_level_auction();
  ghost_winner_for_top_level_auction->set_ad_render_url(
      std::move(inputs.ad_render_url));
  auto* ad_component_render_urls =
      ghost_winner_for_top_level_auction->mutable_ad_component_render_urls();
  *ad_component_render_urls->Add() = std::move(inputs.ad_component_render_url);
  ghost_winner_for_top_level_auction->set_modified_bid(inputs.modified_bid);
  ghost_winner_for_top_level_auction->set_bid_currency(
      std::move(inputs.bid_currency));
  ghost_winner_for_top_level_auction->set_ad_metadata(
      std::move(inputs.ad_metadata));
  ghost_winner_for_top_level_auction->set_buyer_and_seller_reporting_id(
      std::move(inputs.buyer_and_seller_reporting_id));
  ghost_winner_for_top_level_auction->set_buyer_reporting_id(
      std::move(inputs.buyer_reporting_id));
  ghost_winner_for_top_level_auction
      ->set_selected_buyer_and_seller_reporting_id(
          std::move(inputs.selected_buyer_and_seller_reporting_id));
  std::vector<AuctionResult::KAnonGhostWinner> ghost_winners = {
      std::move(kanon_ghost_winner)};

  return std::make_unique<KAnonAuctionResultData>(KAnonAuctionResultData{
      .kanon_ghost_winners =
          std::make_unique<KAnonGhostWinners>(std::move(ghost_winners)),
      .kanon_winner_join_candidates = std::make_unique<KAnonJoinCandidate>(
          std::move(k_anon_join_candidate)),
      .kanon_winner_positional_index = inputs.winner_positional_index});
}

}  // namespace privacy_sandbox::bidding_auction_servers
