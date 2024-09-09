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
#ifndef SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_TEST_UTIL_H_
#define SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_TEST_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "api/bidding_auction_servers.pb.h"

namespace privacy_sandbox::bidding_auction_servers {

using HighestScoringOtherBidsMap =
    ::google::protobuf::Map<std::string, google::protobuf::ListValue>;

// Creates a test Bucket128Bit object.
Bucket128Bit GetTestBucket128Bit();

// Creates a test PrivateAggregateContribution object with int32 value and
// Bucket128Bit.
PrivateAggregateContribution GetTestContributionWithIntegers(
    EventType event_type, absl::string_view event_name);

// Creates a test SignalValue object.
SignalValue GetTestSignalValue();

// Creates a test SignalBucket object.
SignalBucket GetTestSignalBucket();

// Creates a test PrivateAggregateContribution object with SignalValue and
// SignalBucket.
PrivateAggregateContribution GetTestContributionWithSignalObjects(
    EventType event_type, absl::string_view event_name);

// Creates a test PrivateAggregateReportingResponse object.
PrivateAggregateReportingResponse GetTestPrivateAggregateResponse(
    std::vector<PrivateAggregateContribution>& contributions,
    absl::string_view ad_tech_origin);

// Creates a test AdWithBid object with input interest group name.
AdWithBid GetAdWithBidWithPrivateAggregationContributions(
    absl::string_view ig_name);

// Creates test pointer to GetBidsRawResponse object.
GetBidsResponse::GetBidsRawResponse
GetBidsRawResponseWithPrivateAggregationContributions(
    absl::string_view ig_name_win, absl::string_view ig_name_loss);

// Creates test highest scoring other bids map with one value.
HighestScoringOtherBidsMap GetHighestScoringOtherBidsMap(
    absl::string_view ig_owner);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_TEST_UTIL_H_
