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
#include "services/common/private_aggregation/private_aggregation_test_util.h"

#include <string>
#include <utility>
#include <vector>

#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

Bucket128Bit GetTestBucket128Bit() {
  Bucket128Bit bucket;
  // Set default values for the first 64 bits
  bucket.add_bucket_128_bits(0x123456789ABCDEF0);
  // Set default values for the second 64 bits
  bucket.add_bucket_128_bits(0x0123456789ABCDEF);
  return bucket;
}

PrivateAggregateContribution GetTestContributionWithIntegers(
    EventType event_type, absl::string_view event_name = "") {
  // Create the example Bucket128Bit
  Bucket128Bit bucket_128_bit = GetTestBucket128Bit();

  // Create the PrivateAggregationValue with int32 value
  PrivateAggregationValue private_aggregation_value;
  private_aggregation_value.set_int_value(10);

  // Create the PrivateAggregationBucket with Bucket128Bit
  PrivateAggregationBucket private_aggregation_bucket;
  *private_aggregation_bucket.mutable_bucket_128_bit() = bucket_128_bit;

  // Create a PrivateAggregateContribution and set its fields
  PrivateAggregateContribution contribution;
  *contribution.mutable_bucket() = private_aggregation_bucket;
  *contribution.mutable_value() = private_aggregation_value;
  contribution.mutable_event()->set_event_type(event_type);
  contribution.mutable_event()->set_event_name(event_name);
  return contribution;
}

SignalValue GetTestSignalValue() {
  SignalValue signal_value;
  signal_value.set_base_value(BASE_VALUE_WINNING_BID);  // Set the base_value
  signal_value.set_offset(10);                          // Set some offset value
  signal_value.set_scale(1.5);                          // Set scale factor
  return signal_value;
}

SignalBucket GetTestSignalBucket() {
  SignalBucket signal_bucket;
  signal_bucket.set_base_value(
      BASE_VALUE_HIGHEST_SCORING_OTHER_BID);  // Set the base_value
  signal_bucket.set_scale(2.0);               // Set scale factor
  return signal_bucket;
}

PrivateAggregateContribution GetTestContributionWithSignalObjects(
    EventType event_type, absl::string_view event_name = "") {
  // Create the example SignalValue
  SignalValue signal_value = GetTestSignalValue();

  // Create the example SignalBucket
  SignalBucket signal_bucket = GetTestSignalBucket();

  // Create the BucketOffset
  BucketOffset bucket_offset;
  bucket_offset.add_value(static_cast<uint64_t>(1));  // Add 64-bit values
  bucket_offset.add_value(
      static_cast<uint64_t>(0));        // Add another 64-bit value
  bucket_offset.set_is_negative(true);  // Set the is_negative flag

  // Set the BucketOffset in SignalBucket
  *signal_bucket.mutable_offset() = bucket_offset;

  // Create the PrivateAggregationValue with SignalValue
  PrivateAggregationValue private_aggregation_value;
  *private_aggregation_value.mutable_extended_value() = signal_value;

  // Create the PrivateAggregationBucket with SignalBucket
  PrivateAggregationBucket private_aggregation_bucket;
  *private_aggregation_bucket.mutable_signal_bucket() = signal_bucket;

  // Create a PrivateAggregateContribution and set its fields
  PrivateAggregateContribution contribution;
  *contribution.mutable_bucket() = private_aggregation_bucket;
  *contribution.mutable_value() = private_aggregation_value;
  contribution.mutable_event()->set_event_type(event_type);
  contribution.mutable_event()->set_event_name(event_name);
  return contribution;
}

PrivateAggregateReportingResponse GetTestPrivateAggregateResponse(
    std::vector<PrivateAggregateContribution>& contributions,
    absl::string_view ad_tech_origin) {
  PrivateAggregateReportingResponse response;
  for (const auto& contribution : contributions) {
    *response.add_contributions() = contribution;
  }
  response.set_adtech_origin(ad_tech_origin);
  return response;
}

AdWithBid GetAdWithBidWithPrivateAggregationContributions(
    absl::string_view ig_name) {
  PrivateAggregateContribution win_object_contribution =
      GetTestContributionWithSignalObjects(EVENT_TYPE_WIN, "");
  PrivateAggregateContribution loss_object_contribution =
      GetTestContributionWithSignalObjects(EVENT_TYPE_LOSS, "");
  PrivateAggregateContribution win_int_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_WIN, "");
  PrivateAggregateContribution loss_int_contribution =
      GetTestContributionWithIntegers(EVENT_TYPE_LOSS, "");
  AdWithBid ad_with_bid;
  *ad_with_bid.add_private_aggregation_contributions() =
      win_object_contribution;
  *ad_with_bid.add_private_aggregation_contributions() =
      loss_object_contribution;
  *ad_with_bid.add_private_aggregation_contributions() = win_int_contribution;
  *ad_with_bid.add_private_aggregation_contributions() = loss_int_contribution;
  ad_with_bid.set_interest_group_name(ig_name);
  return ad_with_bid;
}

// Returns reserved.win and reserved.loss Private Aggregation Contributions
// for winning and lossing interest groups.
GetBidsResponse::GetBidsRawResponse
GetBidsRawResponseWithPrivateAggregationContributions(
    absl::string_view ig_name_win, absl::string_view ig_name_loss) {
  GetBidsResponse::GetBidsRawResponse get_bids_raw_response;
  AdWithBid ad_with_bid_win =
      GetAdWithBidWithPrivateAggregationContributions(ig_name_win);
  AdWithBid ad_with_bid_loss =
      GetAdWithBidWithPrivateAggregationContributions(ig_name_loss);

  *get_bids_raw_response.add_bids() = ad_with_bid_win;
  *get_bids_raw_response.add_bids() = ad_with_bid_loss;

  return get_bids_raw_response;
}

HighestScoringOtherBidsMap GetHighestScoringOtherBidsMap(
    absl::string_view ig_owner) {
  HighestScoringOtherBidsMap ig_owner_highest_scoring_other_bids_map;
  ig_owner_highest_scoring_other_bids_map.try_emplace(
      ig_owner, google::protobuf::ListValue());
  ig_owner_highest_scoring_other_bids_map.at(ig_owner)
      .add_values()
      ->set_number_value(1.0);
  return ig_owner_highest_scoring_other_bids_map;
}

}  // namespace privacy_sandbox::bidding_auction_servers
