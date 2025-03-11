//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#ifndef SERVICES_AUCTION_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_MANAGER_TEST_UTIL_H_
#define SERVICES_AUCTION_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_MANAGER_TEST_UTIL_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/auction_service/private_aggregation/private_aggregation_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

using RejectionReasonMap = absl::flat_hash_map<
    std::string, absl::flat_hash_map<std::string, SellerRejectionReason>>;

using AdWithBidMetadataMap = absl::flat_hash_map<
    std::string,
    std::unique_ptr<ScoreAdsRequest::ScoreAdsRawRequest::AdWithBidMetadata>>;

inline constexpr absl::string_view kTestIgOwner1 = "ig_owner_1";
inline constexpr absl::string_view kTestIgName1 = "ig_name_1";

// Winning and Losing Dispatch Request ID for testing.
inline constexpr absl::string_view kTestWinningId = "1234";
inline constexpr absl::string_view kTestLossId = "5678";
inline constexpr absl::string_view kTestCustomEvent = "custom_event_1";

inline constexpr float kTestBidInSellerCurrency = 10.0f;
inline constexpr float kTestBidInBuyerCurrency = 7.5f;
inline constexpr float kTestHighestScoringOtherBidValue = 5.0f;

// Helper function to test AppendAdEventContributionsToPaggResponse with
// different input values.
void ValidatePAGGContributions(EventType event_type,
                               const std::string& json_string,
                               BaseValues& base_values,
                               int expected_contribution_count,
                               absl::StatusCode expected_status_code);

// Helper function to parse JSON and test ParseAndProcessContribution
void TestParseAndProcessContribution(const BaseValues& base_values,
                                     const std::string& json_string,
                                     int expected_int_value,
                                     uint64_t expected_lower_bits,
                                     uint64_t expected_upper_bits,
                                     absl::StatusCode expected_status_code);

// Helper function to parse JSON and test ParseSignalValue
void TestParseSignalValue(const std::string& json_string,
                          BaseValue expected_base_value, int expected_offset,
                          double expected_scale,
                          absl::StatusCode expected_status_code);

// Helper function to parse JSON and test ParseSignalBucket
void TestParseSignalBucket(const std::string& json_string,
                           BaseValue expected_base_value,
                           int64_t expected_offset_0, int64_t expected_offset_1,
                           bool expected_is_negative, double expected_scale,
                           absl::StatusCode expected_status_code);

// Helper function to parse JSON and test ParseBucketOffset
void TestParseBucketOffset(absl::string_view json_string,
                           int64_t expected_value_0, int64_t expected_value_1,
                           bool expected_is_negative,
                           absl::StatusCode expected_status_code);

// Helper function to create rapidjson Value for SignalValue
rapidjson::Value CreateSignalValue(
    absl::string_view base_value, int offset, double scale,
    rapidjson::Document::AllocatorType& allocator);

// Creates a rapidjson::Document representing a Private Aggregation API
// contribution response.
//
// `win_contributions`: A vector of PrivateAggregateContribution objects for the
// "win" event type.
// `loss_contributions`: A vector of PrivateAggregateContribution objects for
// the "loss" event type. `always_contributions`: A vector of
// PrivateAggregateContribution objects for the "always" event type.
// `custom_event_contributions`: A map of custom event names to their
//  corresponding vectors of PrivateAggregateContribution objects.
//
//  Returns a rapidjson::Document representing the contribution response.
//  The document will have the following structure:
// ```javascript
// {
//   "win": [ /* ... array of win contributions */ ],
//   "loss": [ /* ... array of loss contributions */ ],
//   "always": [ /* ... array of always contributions */ ],
//   "custom_events": {
//     "custom_event_1": [ /* ... array of contributions */ ],
//     "custom_event_2": [ /* ... array of contributions */ ]
//     // ... more custom events
//   }
// }
// ```
rapidjson::Document CreateContributionResponseDocument(
    const std::vector<PrivateAggregateContribution>& win_contributions,
    const std::vector<PrivateAggregateContribution>& loss_contributions,
    const std::vector<PrivateAggregateContribution>& always_contributions,
    const absl::flat_hash_map<std::string,
                              std::vector<PrivateAggregateContribution>>&
        custom_event_contributions);

// Creates a BaseValues struct with default values for testing.
// The default values are:
//   winning_bid: 10.0f
//   highest_scoring_other_bid: 5.0f
//   reject_reason: SellerRejectionReason::INVALID_BID
BaseValues MakeDefaultBaseValues();

// Creates a BaseValues struct with default values for testing, but without
// setting the `reject_reason` field.
// The default values are:
//   winning_bid: 10.0f
//   highest_scoring_other_bid: 5.0f
BaseValues MakeDefaultBaseValuesWithNoRejectionReason();

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_MANAGER_TEST_UTIL_H_
