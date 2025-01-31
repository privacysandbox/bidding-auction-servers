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

#include "services/auction_service/private_aggregation/private_aggregation_manager.h"

#include <string>
#include <utility>

#include "api/bidding_auction_servers.pb.h"
#include "include/rapidjson/document.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/private_aggregation/private_aggregation_post_auction_util.h"
#include "services/common/util/json_util.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

// Processes a list of Private Aggregation API contributions from an
// array of contributions and append them to `pagg_response`.
//
// contributions: A rapidjson ConstArray object representing an array of
// PrivateAggregation contributions.
// base_values: The base values to use for processing extended values.
// pagg_response: The PrivateAggregateReportingResponse object to append the
// processed contributions to.
void ProcessAndAppendContributionsToPaggResponse(
    const rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray& contributions,
    const BaseValues& base_values,
    PrivateAggregateReportingResponse& pagg_response) {
  for (const auto& event_obj : contributions) {
    absl::StatusOr<PrivateAggregateContribution> new_contribution =
        ParseAndProcessContribution(base_values, event_obj);
    if (!new_contribution.ok()) {
      // TODO(b/362803378): Add metrics for PAgg contributions.
      PS_VLOG(kNoisyWarn) << "Invalid contribution: "
                          << new_contribution.status();
      continue;
    }
    *pagg_response.add_contributions() = std::move(new_contribution.value());
  }
}

void ProcessAndAppendEventContributionsToPaggResponse(
    absl::string_view event_name,
    const rapidjson::GenericValue<rapidjson::UTF8<>>::ConstArray& contributions,
    const BaseValues& base_values,
    PrivateAggregateReportingResponse& pagg_response) {
  for (const auto& event_obj : contributions) {
    absl::StatusOr<PrivateAggregateContribution> new_contribution =
        ParseAndProcessContribution(base_values, event_obj);
    if (!new_contribution.ok()) {
      // TODO(b/362803378): Add metrics for PAgg contributions.
      PS_VLOG(kNoisyWarn) << "Invalid contribution: "
                          << new_contribution.status();
      continue;
    }
    PrivateAggregationEvent event;
    event.set_event_type(EVENT_TYPE_CUSTOM);
    event.set_event_name(event_name);
    *new_contribution->mutable_event() = event;
    *pagg_response.add_contributions() = std::move(new_contribution.value());
  }
}

// Parses and processes contributions for a specific ad event type and appends
// them to the `private_aggregation_reporting_response`.
//
// event_type: The type of ad event (e.g., win, loss, always).
// id: The ID of the ad.
// contribution_obj_doc: The JSON document containing the contributions for
//     the specified event type.
// base_values: The BaseValues struct to use for processing extended values.
// private_aggregation_reporting_response: The PrivateAggregateReportingResponse
//     object to append the processed contributions to.
void HandleContribution(
    EventType event_type, const BaseValues& base_values,
    const rapidjson::Document& contribution_obj_doc,
    PrivateAggregateReportingResponse& private_aggregation_reporting_response) {
  absl::Status result = AppendAdEventContributionsToPaggResponse(
      event_type, contribution_obj_doc, base_values,
      private_aggregation_reporting_response);
  if (!result.ok()) {
    PS_VLOG(kNoisyWarn) << "Error from Parsing Contributions from ID "
                        << result;
  }
}

}  // namespace

BaseValue ToBaseValue(absl::string_view base_value_str) {
  if (base_value_str.empty()) {
    return BaseValue::BASE_VALUE_UNSPECIFIED;
  }

  if (base_value_str == kBaseValueWinningBid) {
    return BaseValue::BASE_VALUE_WINNING_BID;
  } else if (base_value_str == kBaseValueHighestScoringOtherBid) {
    return BaseValue::BASE_VALUE_HIGHEST_SCORING_OTHER_BID;
  } else if (base_value_str == kBaseValueScriptRunTime) {
    return BaseValue::BASE_VALUE_SCRIPT_RUN_TIME;
  } else if (base_value_str == kBaseValueSignalsFetchTime) {
    return BaseValue::BASE_VALUE_SIGNALS_FETCH_TIME;
  } else if (base_value_str == kBaseValueBidRejectionReason) {
    return BaseValue::BASE_VALUE_BID_REJECTION_REASON;
  } else {
    return BaseValue::BASE_VALUE_UNSPECIFIED;
  }
}

// The return absl::string_view must be a global inline variable
// See https://abseil.io/tips/168 and https://abseil.io/tips/140
absl::string_view EventTypeToString(EventType event_type) {
  switch (event_type) {
    case EventType::EVENT_TYPE_WIN:
      return kEventTypeWin;
    case EventType::EVENT_TYPE_LOSS:
      return kEventTypeLoss;
    case EventType::EVENT_TYPE_ALWAYS:
      return kEventTypeAlways;
    case EventType::EVENT_TYPE_CUSTOM:
      return kEventTypeCustom;
    default:
      return kEventTypeUnspecified;
  }
}

void HandlePrivateAggregationReporting(
    const PrivateAggregationHandlerMetadata& metadata,
    const rapidjson::Document& contribution_obj_doc,
    ScoreAdsResponse::AdScore& score_ads_response) {
  PrivateAggregateReportingResponse private_aggregation_reporting_response;
  // Parses contribution_obj_doc based on whether the ad won or lost
  // Use the static function instead of the lambda
  if (metadata.ad_id != metadata.most_desirable_ad_score_id) {
    HandleContribution(EventType::EVENT_TYPE_LOSS, metadata.base_values,
                       contribution_obj_doc,
                       private_aggregation_reporting_response);
    HandleContribution(EventType::EVENT_TYPE_ALWAYS, metadata.base_values,
                       contribution_obj_doc,
                       private_aggregation_reporting_response);
  } else {
    HandleContribution(EventType::EVENT_TYPE_WIN, metadata.base_values,
                       contribution_obj_doc,
                       private_aggregation_reporting_response);
    HandleContribution(EventType::EVENT_TYPE_ALWAYS, metadata.base_values,
                       contribution_obj_doc,
                       private_aggregation_reporting_response);
    HandleContribution(EventType::EVENT_TYPE_CUSTOM, metadata.base_values,
                       contribution_obj_doc,
                       private_aggregation_reporting_response);
  }
  if (metadata.seller_origin) {
    private_aggregation_reporting_response.set_adtech_origin(
        *metadata.seller_origin);
  }
  score_ads_response.add_top_level_contributions()->Swap(
      &private_aggregation_reporting_response);
}

PrivateAggregateReportingResponse GetPrivateAggregateReportingResponseForWinner(
    const BaseValues& base_values,
    const rapidjson::Document& contribution_obj_doc) {
  PrivateAggregateReportingResponse private_aggregation_reporting_response;

  HandleContribution(EventType::EVENT_TYPE_WIN, base_values,
                     contribution_obj_doc,
                     private_aggregation_reporting_response);
  HandleContribution(EventType::EVENT_TYPE_ALWAYS, base_values,
                     contribution_obj_doc,
                     private_aggregation_reporting_response);
  HandleContribution(EventType::EVENT_TYPE_CUSTOM, base_values,
                     contribution_obj_doc,
                     private_aggregation_reporting_response);
  return private_aggregation_reporting_response;
}

absl::Status AppendAdEventContributionsToPaggResponse(
    EventType event_type, const rapidjson::Value& contributions,
    const BaseValues& base_values,
    PrivateAggregateReportingResponse& pagg_response) {
  if (event_type == EventType::EVENT_TYPE_UNSPECIFIED) {
    return absl::InvalidArgumentError("Event type unspecified.");
  }

  absl::string_view event_type_str = EventTypeToString(event_type);

  // data() and length() are used because event_type_str is a global inline
  // constant.
  auto contributions_it = contributions.FindMember(event_type_str.data());

  if (contributions_it == contributions.MemberEnd()) {
    return absl::InternalError(absl::StrCat(
        "Unexpected response from Private Aggregation API execution. "
        "\"event_type\" field not found for event type: ",
        event_type_str));
  }
  const auto& contributions_obj = contributions_it->value;

  if (event_type == EventType::EVENT_TYPE_CUSTOM) {
    for (const auto& member : contributions_obj.GetObject()) {
      const rapidjson::Value& event_array = member.value;
      if (!event_array.IsArray()) {
        absl::StatusOr<std::string> result = SerializeJsonDoc(event_array);
        if (result.ok()) {
          PS_VLOG(kNoisyWarn) << "Custom Event " << member.name.GetString()
                              << " does not contain an array. Value of the "
                                 "custom event field: \n"
                              << result.value();
        } else {
          PS_VLOG(kNoisyWarn)
              << "Custom Event " << member.name.GetString()
              << " does not contain an array. And there's an error "
                 "logging value of the field: "
              << result.status();
        }

        continue;
      }
      std::string event_name = member.name.GetString();
      ProcessAndAppendEventContributionsToPaggResponse(
          event_name, event_array.GetArray(), base_values, pagg_response);
    }
  } else {
    // Handle win, loss, always events
    if (!contributions_obj.IsArray()) {
      absl::StatusOr<std::string> result = SerializeJsonDoc(contributions_obj);
      if (result.ok()) {
        PS_VLOG(kNoisyWarn) << "Reserved Event " << event_type_str
                            << " does not contain an array. Value of the "
                               "reserved event field: \n"
                            << result.value();
      } else {
        PS_VLOG(kNoisyWarn)
            << "Reserved Event " << event_type_str
            << " does not contain an array. And there's an error "
               "logging value of the field: "
            << result.status();
      }
    }
    ProcessAndAppendContributionsToPaggResponse(contributions_obj.GetArray(),
                                                base_values, pagg_response);
  }

  return absl::OkStatus();
}

absl::StatusOr<PrivateAggregateContribution> ParseAndProcessContribution(
    const BaseValues& base_values, const rapidjson::Value& pagg_response) {
  PrivateAggregateContribution contribution;
  std::optional<int> int_value;
  SignalValue signal_value;

  auto value_it = pagg_response.FindMember(kPrivateAggregationValue.data());
  if (value_it == pagg_response.MemberEnd()) {
    return absl::InvalidArgumentError(kInvalidPrivateAggregationValueType);
  }

  PS_ASSIGN_IF_PRESENT(int_value, value_it->value, kSignalValueIntValue, Int64);
  if (int_value) {
    contribution.mutable_value()->set_int_value(*int_value);
  } else if (!value_it->value.HasMember(kSignalValueExtendedValue)) {
    return absl::InvalidArgumentError(kInvalidPrivateAggregationValueType);
  } else {
    PS_ASSIGN_OR_RETURN(
        signal_value,
        ParseSignalValue(value_it->value[kSignalValueExtendedValue]),
        absl::InvalidArgumentError(kInvalidPrivateAggregationValueType));
    contribution.mutable_value()->mutable_extended_value()->Swap(&signal_value);
    PS_ASSIGN_OR_RETURN(int final_int_value,
                        GetPrivateAggregationValuePostAuction(
                            base_values, contribution.value()));
    contribution.mutable_value()->set_int_value(final_int_value);
  }

  // Parse SignalBucket if present.
  auto bucket_it = pagg_response.FindMember(kPrivateAggregationBucket.data());
  if (bucket_it != pagg_response.MemberEnd()) {
    auto signal_bucket_it =
        bucket_it->value.FindMember(kSignalBucketSignalBucket);
    auto bucket_128_bit_it =
        bucket_it->value.FindMember(kSignalBucketBucket128Bit);
    if (signal_bucket_it != bucket_it->value.MemberEnd()) {
      PS_ASSIGN_OR_RETURN(
          auto signal_bucket,
          ParseSignalBucket(bucket_it->value[kSignalBucketSignalBucket]),
          absl::InvalidArgumentError(kInvalidPrivateAggregationBucketType));
      contribution.mutable_bucket()->mutable_signal_bucket()->Swap(
          &signal_bucket);
    } else if (bucket_128_bit_it != bucket_it->value.MemberEnd() &&
               bucket_128_bit_it->value.IsObject()) {
      PS_ASSIGN_OR_RETURN(
          auto bucket_128_bit,
          GetArrayMember(bucket_128_bit_it->value,
                         kSignalBucketBucket128BitBucket128Bits),
          absl::InvalidArgumentError(kInvalidPrivateAggregationBucketType));
      for (auto& bit : bucket_128_bit) {
        contribution.mutable_bucket()
            ->mutable_bucket_128_bit()
            ->add_bucket_128_bits(bit.GetUint64());
      }
    } else {
      return absl::InvalidArgumentError(kInvalidPrivateAggregationBucketType);
    }
    PS_ASSIGN_OR_RETURN(Bucket128Bit final_bucket_128_bits,
                        GetPrivateAggregationBucketPostAuction(
                            base_values, contribution.bucket()));
    contribution.mutable_bucket()->mutable_bucket_128_bit()->Swap(
        &final_bucket_128_bits);
  }

  return contribution;
}

absl::StatusOr<SignalValue> ParseSignalValue(
    const rapidjson::Value& signal_value_json) {
  SignalValue signal_value;
  std::optional<absl::string_view> base_value_str;

  PS_ASSIGN_OR_RETURN(base_value_str, GetStringMember(signal_value_json,
                                                      kSignalValueBaseValue));
  PS_ASSIGN_OR_RETURN(int offset,
                      GetIntMember(signal_value_json, kSignalValueOffset));
  PS_ASSIGN_OR_RETURN(double scale,
                      GetDoubleMember(signal_value_json, kSignalValueScale));

  signal_value.set_base_value(ToBaseValue(*base_value_str));
  signal_value.set_offset(offset);
  signal_value.set_scale(scale);

  return signal_value;
}

absl::StatusOr<SignalBucket> ParseSignalBucket(
    const rapidjson::Value& signal_bucket_json) {
  SignalBucket signal_bucket;
  std::optional<absl::string_view> base_value_str;

  auto bucket_offset_it = signal_bucket_json.FindMember(rapidjson::StringRef(
      kSignalBucketOffset.data(), kSignalBucketOffset.length()));

  if (bucket_offset_it == signal_bucket_json.MemberEnd()) {
    return absl::InvalidArgumentError("Missing Signal Bucket's offset");
  }

  PS_ASSIGN_OR_RETURN(BucketOffset offset,
                      ParseBucketOffset(bucket_offset_it->value));

  PS_ASSIGN_OR_RETURN(base_value_str, GetStringMember(signal_bucket_json,
                                                      kSignalBucketBaseValue));
  PS_ASSIGN_OR_RETURN(double scale,
                      GetDoubleMember(signal_bucket_json, kSignalBucketScale));

  signal_bucket.set_base_value(ToBaseValue(*base_value_str));
  signal_bucket.mutable_offset()->Swap(&offset);
  signal_bucket.set_scale(scale);

  return signal_bucket;
}

absl::StatusOr<BucketOffset> ParseBucketOffset(
    const rapidjson::Value& signal_bucket_json) {
  BucketOffset bucket_offset;

  PS_ASSIGN_OR_RETURN(auto value_array,
                      GetArrayMember(signal_bucket_json,
                                     std::string(kBucketOffsetValue).c_str()));
  PS_ASSIGN_OR_RETURN(bool is_negative, GetBoolMember(signal_bucket_json,
                                                      kBucketOffsetIsNegative));

  bucket_offset.add_value(value_array[0].GetUint64());
  bucket_offset.add_value(value_array[1].GetUint64());
  bucket_offset.set_is_negative(is_negative);
  return bucket_offset;
}

}  // namespace privacy_sandbox::bidding_auction_servers
