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

#ifndef SERVICES_AUCTION_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_MANAGER_H_
#define SERVICES_AUCTION_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"
#include "services/common/code_dispatch/code_dispatch_reactor.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kBaseValueWinningBid =
    "BASE_VALUE_WINNING_BID";
inline constexpr absl::string_view kBaseValueHighestScoringOtherBid =
    "BASE_VALUE_HIGHEST_SCORING_OTHER_BID";
inline constexpr absl::string_view kBaseValueScriptRunTime =
    "BASE_VALUE_SCRIPT_RUN_TIME";
inline constexpr absl::string_view kBaseValueSignalsFetchTime =
    "BASE_VALUE_SIGNALS_FETCH_TIME";
inline constexpr absl::string_view kBaseValueBidRejectionReason =
    "BASE_VALUE_BID_REJECTION_REASON";
inline constexpr absl::string_view kPrivateAggregateContributionMissingValue =
    "Missing value in contribution.";

inline constexpr char kSignalValueIntValue[] = "int_value";
inline constexpr char kSignalValueExtendedValue[] = "extended_value";
inline constexpr char kSignalBucketBucket128Bit[] = "bucket_128_bit";
inline constexpr char kSignalBucketBucket128BitBucket128Bits[] =
    "bucket_128_bits";
inline constexpr char kSignalBucketSignalBucket[] = "signal_bucket";

inline constexpr absl::string_view kPrivateAggregationBucket = "bucket";
inline constexpr absl::string_view kPrivateAggregationValue = "value";

inline constexpr absl::string_view kSignalValueBaseValue = "base_value";
inline constexpr absl::string_view kSignalValueScale = "scale";
inline constexpr absl::string_view kSignalValueOffset = "offset";

inline constexpr absl::string_view kSignalBucketBaseValue = "base_value";
inline constexpr absl::string_view kSignalBucketScale = "scale";
inline constexpr absl::string_view kSignalBucketOffset = "offset";

inline constexpr absl::string_view kBucketOffsetValue = "value";
inline constexpr absl::string_view kBucketOffsetIsNegative = "is_negative";

inline constexpr absl::string_view kEventTypeWin = "win";
inline constexpr absl::string_view kEventTypeLoss = "loss";
inline constexpr absl::string_view kEventTypeAlways = "always";
inline constexpr absl::string_view kEventTypeCustom = "custom_events";
inline constexpr absl::string_view kEventTypeUnspecified = "not-specified";

using RawRequest = ScoreAdsRequest::ScoreAdsRawRequest;

struct PrivateAggregationHandlerMetadata {
  std::string most_desirable_ad_score_id;
  int private_aggregate_report_limit;
  BaseValues base_values;
  std::string ad_id;
  std::optional<std::string> seller_origin;
};

// Convert baseValue string into enum.
// For more information, see
// https://github.com/WICG/turtledove/blob/main/FLEDGE_extended_PA_reporting.md#reporting-api-informal-specification.
BaseValue ToBaseValue(absl::string_view base_value_str);

// Convert EventType enum into string.
// Default is "not-specified".
absl::string_view EventTypeToString(EventType event_type);

//  This function handles parsing,filtering and processing of paapicontributions
// exported from Roma:
// - Parses paapi_contributions_doc based on whether the ad won or lost.
// - Processes the contributions to convert SignalBucket/SignalValue into
// numeric type.
// - Populates the contributions in the winning_ad_score
void HandlePrivateAggregationReporting(
    const PrivateAggregationHandlerMetadata& metadata,
    const rapidjson::Document& contribution_obj_doc,
    ScoreAdsResponse::AdScore& score_ads_response);

// Parses contribution_obj_doc and generates PrivateAggregateReportingResponse
// for the winning ig. Only the contributions for reserved.win, reserved.always
// and custom events will be processed.
PrivateAggregateReportingResponse GetPrivateAggregateReportingResponseForWinner(
    const BaseValues& base_values,
    const rapidjson::Document& contribution_obj_doc);

// Parses, Processes, and Appends the selected contributions (win, loss,
// always, or custom_events) to input pagg_response.
absl::Status AppendAdEventContributionsToPaggResponse(
    EventType event_type, const rapidjson::Value& contributions,
    const BaseValues& base_values,
    PrivateAggregateReportingResponse& pagg_response);

// Parses and processes a JSON containing Private Aggregate Contribution Object
// and returns a PrivateAggregateContribution object.
//
// SignalBucket and SignalValue will be converted to numerical value as
// base_value * scale + offset.
absl::StatusOr<PrivateAggregateContribution> ParseAndProcessContribution(
    const BaseValues& base_values, const rapidjson::Value& pagg_response);

// Parses a SignalValue object from a rapidjson::Document.
//
// signal_value_json: The rapidjson::Value node from Private Aggregation
// Response that contains SignalValue data.
//
// Returns the parsed and processed SignalValue.
// This function assumes input JSON string contains all required fields for
// SignalValue, and all values are valid.
absl::StatusOr<SignalValue> ParseSignalValue(
    const rapidjson::Value& signal_value_json);

// Parses a SignalBucket object from a rapidjson::Value node.
//
// signal_bucket_json: The rapidjson::Value node from Private Aggregation
// Response that contains SignalBucket data.
// - Example: `{"offset": {"value": [1, 2],"is_negative": true}, "base_value":
// "BASE_VALUE_WINNING_BID","scale": 2.5}`
//
// Returns the parsed and processed SignalBucket.
absl::StatusOr<SignalBucket> ParseSignalBucket(
    const rapidjson::Value& signal_bucket_json);

// Parses a BucketOffset object from a rapidjson::Value node that contains
// Bucket Offset's fields.
absl::StatusOr<BucketOffset> ParseBucketOffset(
    const rapidjson::Value& signal_bucket_json);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_AUCTION_SERVICE_PRIVATE_AGGREGATION_PRIVATE_AGGREGATION_MANAGER_H_
