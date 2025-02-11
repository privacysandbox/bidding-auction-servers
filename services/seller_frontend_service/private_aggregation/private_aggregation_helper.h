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
#ifndef SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "api/bidding_auction_servers.pb.h"
#include "services/common/util/scoped_cbor.h"
#include "services/seller_frontend_service/data/scoring_signals.h"
#include "services/seller_frontend_service/util/cbor_common_util.h"

namespace privacy_sandbox::bidding_auction_servers {

using ContributionsPerAdTechMap =
    absl::flat_hash_map<std::string,
                        std::vector<const PrivateAggregateContribution*>>;

using ContributionsPerEventTypeMap =
    absl::flat_hash_map<std::string,
                        std::vector<const PrivateAggregateContribution*>>;

using ContributionsPerIgTypeMap =
    absl::flat_hash_map<int, std::vector<const PrivateAggregateContribution*>>;

using PrivateAggregateReportingResponses =
    ::google::protobuf::RepeatedPtrField<PrivateAggregateReportingResponse>;

using AdScores =
    ::google::protobuf::RepeatedPtrField<ScoreAdsResponse::AdScore>;

struct InterestGroupIdentity {
  std::string interest_group_owner;
  std::string interest_group_name;
  bool operator==(const InterestGroupIdentity& other) const {
    return interest_group_owner == other.interest_group_owner &&
           interest_group_name == other.interest_group_name;
  }

  // Enable Abseil hashing
  template <typename H>
  friend H AbslHashValue(H h, const InterestGroupIdentity& ig) {
    return H::combine(std::move(h), ig.interest_group_owner,
                      ig.interest_group_name);
  }
};

// Handles private aggregation contributions filtering and post processing.
void HandlePrivateAggregationContributions(
    const absl::flat_hash_map<InterestGroupIdentity, int>&
        interest_group_index_map,
    ScoreAdsResponse::AdScore& high_score,
    BuyerBidsResponseMap& shared_buyer_bids_map);

// Groups PrivateAggregateContributions per AdTech
ContributionsPerAdTechMap GroupContributionsByAdTech(
    int per_adtech_paapi_contributions_limit,
    const PrivateAggregateReportingResponses& responses);

// Converts array of 64 bit integers in Bucket128Bit to
// byte string format in big endian order.
std::string ConvertIntArrayToByteString(const PrivateAggregationBucket& bucket);

// Serializes PrivateAggregateContribution. If PrivateAggregationBucket or
// PrivateAggregationValue is not present or invalid, the contribution will be
// dropped.
absl::Status CborSerializePAggContribution(
    const PrivateAggregateContribution& contribution,
    ErrorHandler error_handler, cbor_item_t& root);

// Groups PrivateAggregateContributions by PrivateAggregationEvent and
// serializes to create paggEventContribution.
absl::Status CborSerializePAggEventContributions(
    const std::vector<const PrivateAggregateContribution*>& contributions,
    ErrorHandler error_handler, cbor_item_t& root);

// Decodes event and PrivateAggregateContribution from serialized
// pAggEventContributions and returns list of PrivateAggregateContribution.
absl::StatusOr<std::vector<PrivateAggregateContribution>>
CborDecodePAggEventContributions(
    std::optional<int> ig_idx,
    cbor_item_t& serialized_pagg_event_contributions);

// Groups PrivateAggregateContributions by ig_idx and serializes
// PrivateAggregateContributions to create igContributions.
absl::Status CborSerializeIgContributions(
    const std::vector<const PrivateAggregateContribution*>& contributions,
    ErrorHandler error_handler, cbor_item_t& root);

// Decodes ig_idx and PrivateAggregateContribution from serialized
// igContributions and returns list of PrivateAggregateContribution.
absl::StatusOr<std::vector<PrivateAggregateContribution>>
CborDecodePAggIgContributions(cbor_item_t& serialized_ig_contributions);

// Groups PrivateAggregateContributions by adtech and serializes
// PrivateAggregateContributions to create paggResponse.
absl::Status CborSerializePAggResponse(
    const PrivateAggregateReportingResponses& responses,
    int per_adtech_paapi_contributions_limit, ErrorHandler error_handler,
    cbor_item_t& root);

// Decodes reporting_origin and PrivateAggregateContributions from serialized
// igContributions and returns list of PrivateAggregateReportingResponse.
absl::StatusOr<PrivateAggregateReportingResponses> CborDecodePAggResponse(
    cbor_item_t& serialized_adtech_contributions);

// Handles private aggregation contributions filtering and post processing for
// ghost winners.
void HandlePrivateAggregationContributionsForGhostWinner(
    const absl::flat_hash_map<InterestGroupIdentity, int>&
        interest_group_index_map,
    ScoreAdsResponse::AdScore& ghost_winning_score,
    BuyerBidsResponseMap& shared_buyer_bids_map);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_HELPER_H_
