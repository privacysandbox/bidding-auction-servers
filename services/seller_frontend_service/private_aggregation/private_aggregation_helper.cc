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
#include "services/seller_frontend_service/private_aggregation/private_aggregation_helper.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "services/common/loggers/request_log_context.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"
#include "services/common/util/reporting_util.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

// Filters the referenced AdWithBid parameter to select win, custom, and
// always contributions. Edits the passed AdWithBid input parameter.
void FilterWinContributions(AdWithBid& ad_with_bid) {
  std::vector<PrivateAggregateContribution> winning_contributions;
  for (auto& contribution :
       *ad_with_bid.mutable_private_aggregation_contributions()) {
    if (contribution.event().event_type() == EventType::EVENT_TYPE_WIN ||
        contribution.event().event_type() == EventType::EVENT_TYPE_CUSTOM ||
        contribution.event().event_type() == EventType::EVENT_TYPE_ALWAYS) {
      winning_contributions.push_back(std::move(contribution));
    }
  }
  ad_with_bid.clear_private_aggregation_contributions();

  for (auto& win_contribution : winning_contributions) {
    *ad_with_bid.add_private_aggregation_contributions() =
        std::move(win_contribution);
  }
}

// Filters the referenced AdWithBid parameter to select loss and always
// contributions. Edits the passed AdWithBid input parameter.
void FilterLossContributions(AdWithBid& ad_with_bid) {
  std::vector<PrivateAggregateContribution> losing_contributions;
  for (auto& contribution :
       *ad_with_bid.mutable_private_aggregation_contributions()) {
    if (contribution.event().event_type() == EventType::EVENT_TYPE_LOSS ||
        contribution.event().event_type() == EventType::EVENT_TYPE_ALWAYS) {
      losing_contributions.push_back(std::move(contribution));
    }
  }

  ad_with_bid.clear_private_aggregation_contributions();

  for (auto& loss_contribution : losing_contributions) {
    *ad_with_bid.add_private_aggregation_contributions() =
        std::move(loss_contribution);
  }
}

BaseValues GetBaseValues(ScoreAdsResponse::AdScore& high_score) {
  BaseValues base_values;
  float highest_scoring_other_bid = 0.0;
  if (high_score.ig_owner_highest_scoring_other_bids_map().size() > 0) {
    auto iterator =
        high_score.ig_owner_highest_scoring_other_bids_map().begin();
    if (!iterator->second.values().empty()) {
      highest_scoring_other_bid =
          iterator->second.values().Get(0).number_value();
    }
  }
  base_values.highest_scoring_other_bid = highest_scoring_other_bid;

  if (high_score.incoming_bid_in_seller_currency() > 0) {
    base_values.winning_bid = high_score.incoming_bid_in_seller_currency();
  } else {
    base_values.winning_bid = high_score.buyer_bid();
  }
  return base_values;
}

std::vector<PrivateAggregateContribution> GetProcessedAndFilteredContributions(
    AdWithBid& ad_with_bid, BaseValues& base_values) {
  std::vector<PrivateAggregateContribution> processed_filtered_contributions;
  for (PrivateAggregateContribution& contribution :
       *ad_with_bid.mutable_private_aggregation_contributions()) {
    absl::StatusOr<int> signal_value_status =
        GetPrivateAggregationValuePostAuction(base_values,
                                              contribution.value());
    absl::StatusOr<Bucket128Bit> signal_bucket_status =
        GetPrivateAggregationBucketPostAuction(base_values,
                                               contribution.bucket());
    if (signal_value_status.ok() && signal_bucket_status.ok()) {
      if (contribution.value().has_extended_value()) {
        contribution.mutable_value()->clear_extended_value();
      }
      contribution.mutable_value()->set_int_value(signal_value_status.value());
      if (contribution.bucket().has_signal_bucket()) {
        contribution.mutable_bucket()->clear_signal_bucket();
      }
      *contribution.mutable_bucket()->mutable_bucket_128_bit() =
          signal_bucket_status.value();
      contribution.clear_event();
      processed_filtered_contributions.push_back(std::move(contribution));
    } else {
      PS_VLOG(4) << "Private Aggregation Contribution dropped.";
    }
  }
  return processed_filtered_contributions;
}

}  // namespace

void HandlePrivateAggregationContributions(
    const absl::flat_hash_map<InterestGroupIdentity, int>&
        interest_group_index_map,
    ScoreAdsResponse::AdScore& high_score,
    BuyerBidsResponseMap& shared_buyer_bids_map) {
  for (auto& [ig_owner, get_bid_response] : shared_buyer_bids_map) {
    PrivateAggregateReportingResponse reporting_response;
    reporting_response.set_adtech_origin(ig_owner);

    for (auto& ad_with_bid : *get_bid_response->mutable_bids()) {
      if (ad_with_bid.private_aggregation_contributions().empty()) {
        continue;
      }
      const std::string& ig_name = ad_with_bid.interest_group_name();
      int ig_idx = 0;
      InterestGroupIdentity ig = {.interest_group_owner = ig_owner,
                                  .interest_group_name = ig_name};
      const auto& ig_itr = interest_group_index_map.find(ig);
      if (ig_itr != interest_group_index_map.end()) {
        ig_idx = ig_itr->second;
      } else {
        PS_LOG(ERROR) << "Skipping PrivateAggregateContributions."
                      << "IG index not found for interest group owner:"
                      << ig_owner << " and interest group name" << ig_name;
        continue;
      }
      BaseValues base_values = GetBaseValues(high_score);

      // Filtering.
      if (high_score.interest_group_owner() == ig_owner &&
          ig_name == high_score.interest_group_name()) {
        FilterWinContributions(ad_with_bid);
      } else {
        FilterLossContributions(ad_with_bid);

        // Iterate over ad_rejection_reasons to find the appropriate rejection
        // reason.
        for (const auto& rejection : high_score.ad_rejection_reasons()) {
          if (rejection.interest_group_owner() == ig_owner &&
              rejection.interest_group_name() == ig_name) {
            base_values.reject_reason = rejection.rejection_reason();
            break;
          }
        }
      }
      if (ad_with_bid.private_aggregation_contributions().empty()) {
        continue;
      }

      std::vector<PrivateAggregateContribution>
          processed_filtered_contributions =
              GetProcessedAndFilteredContributions(ad_with_bid, base_values);

      for (PrivateAggregateContribution& contribution :
           processed_filtered_contributions) {
        contribution.set_ig_idx(ig_idx);
        *reporting_response.add_contributions() = std::move(contribution);
      }
    }
    if (!reporting_response.contributions().empty()) {
      *high_score.add_top_level_contributions() = std::move(reporting_response);
    }
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers
