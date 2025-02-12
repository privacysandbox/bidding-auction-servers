/*
 * Copyright 2024 Google LLC
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

#include "services/common/util/priority_vector/priority_vector_utils.h"

#include <algorithm>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

absl::Duration GetRecency(const BrowserSignalsForBidding& browser_signals) {
  int64_t recency_ms;
  if (browser_signals.has_recency_ms()) {
    recency_ms = browser_signals.recency_ms();
  } else {
    recency_ms = browser_signals.recency() * 1000;
  }

  return absl::Milliseconds(recency_ms);
}

// Calculates dot product of two vectors by multiplying the common entries in
// the two vectors and summing the products.
double CalculateDotProduct(const rapidjson::Document& vector1,
                           const rapidjson::Value& vector2) {
  double priority = 0.0;
  for (auto itr = vector1.MemberBegin(); itr != vector1.MemberEnd(); ++itr) {
    const char* key = itr->name.GetString();

    auto vector2_itr = vector2.FindMember(key);
    if (vector2_itr != vector2.MemberEnd() && itr->value.IsNumber() &&
        vector2_itr->value.IsNumber()) {
      priority += itr->value.GetDouble() * vector2_itr->value.GetDouble();
    }
  }

  return priority;
}

void PopulateDeviceSignalFields(
    rapidjson::Document& priority_signals,
    const BrowserSignalsForBidding& browser_signals) {
  absl::AnyInvocable<void(const char*, double)> add_or_update_value =
      [&priority_signals](const char* key, double value) {
        rapidjson::Value key_value(key, priority_signals.GetAllocator());
        auto itr = priority_signals.FindMember(key_value.GetString());
        if (itr != priority_signals.MemberEnd()) {
          itr->value.SetDouble(value);
        } else {
          priority_signals.AddMember(key_value, value,
                                     priority_signals.GetAllocator());
        }
      };

  add_or_update_value(kDeviceSignalsOne, 1);
  absl::Duration ig_age = GetRecency(browser_signals);

  double ig_age_minutes = absl::ToDoubleMinutes(ig_age);
  add_or_update_value(kDeviceSignalsAgeInMinutes, ig_age_minutes);
  add_or_update_value(kDeviceSignalsAgeInMinutesMax60,
                      std::min(ig_age_minutes, 60.0));

  double ig_age_hours = absl::ToDoubleHours(ig_age);
  add_or_update_value(kDeviceSignalsAgeInHoursMax24,
                      std::min(ig_age_hours, 24.0));

  double ig_age_days = absl::ToDoubleHours(ig_age) / 24;
  add_or_update_value(kDeviceSignalsAgeInDaysMax30,
                      std::min(ig_age_days, 30.0));
}

}  //  namespace

void SanitizePriorityVector(rapidjson::Value& priority_vector) {
  for (auto itr = priority_vector.MemberBegin();
       itr != priority_vector.MemberEnd();) {
    if (!itr->name.IsString() || !itr->value.IsNumber()) {
      itr = priority_vector.EraseMember(itr);
    } else {
      ++itr;
    }
  }
}

absl::StatusOr<rapidjson::Document> ParsePriorityVector(
    const std::string& priority_vector_json) {
  absl::StatusOr<rapidjson::Document> priority_vector =
      ParseJsonString(priority_vector_json);
  if (!priority_vector.ok() || !priority_vector->IsObject()) {
    return absl::InvalidArgumentError("Malformed priority vector JSON");
  }

  SanitizePriorityVector(*priority_vector);
  return *std::move(priority_vector);
}

absl::StatusOr<std::string> GetBuyerPrioritySignals(
    const rapidjson::Document& priority_signals_vector,
    const google::protobuf::Map<std::string,
                                SelectAdRequest::AuctionConfig::PerBuyerConfig>&
        per_buyer_config,
    const std::string& buyer_ig_owner) {
  rapidjson::Document buyer_priority_signals(rapidjson::kObjectType);
  buyer_priority_signals.CopyFrom(priority_signals_vector,
                                  buyer_priority_signals.GetAllocator());

  const auto& per_buyer_config_itr = per_buyer_config.find(buyer_ig_owner);
  if (per_buyer_config_itr == per_buyer_config.end() ||
      per_buyer_config_itr->second.priority_signals_overrides().empty()) {
    return SerializeJsonDoc(buyer_priority_signals);
  }

  absl::StatusOr<rapidjson::Document> priority_signals_overrides =
      ParseJsonString(
          per_buyer_config_itr->second.priority_signals_overrides());
  if (!priority_signals_overrides.ok() ||
      !priority_signals_overrides->IsObject()) {
    std::string error_message = absl::StrCat(
        "Malformed priority signals overrides for buyer: ", buyer_ig_owner);
    PS_VLOG(8) << error_message;
    return absl::InvalidArgumentError(error_message);
  }

  for (auto itr = priority_signals_overrides->MemberBegin();
       itr != priority_signals_overrides->MemberEnd(); ++itr) {
    if (!itr->name.IsString() || !itr->value.IsNumber()) {
      PS_VLOG(8)
          << "Wrong type provided for entry in priority signals overrides";
      continue;
    }

    double value = itr->value.GetDouble();
    auto buyer_priority_signals_itr =
        buyer_priority_signals.FindMember(itr->name);
    if (buyer_priority_signals_itr != buyer_priority_signals.MemberEnd()) {
      buyer_priority_signals_itr->value.SetDouble(value);
    } else {
      buyer_priority_signals.AddMember(itr->name, value,
                                       buyer_priority_signals.GetAllocator());
    }
  }

  return SerializeJsonDoc(buyer_priority_signals);
}

absl::flat_hash_map<std::string, double> CalculateInterestGroupPriorities(
    rapidjson::Document& priority_signals,
    const BuyerInputForBidding& buyer_input,
    const absl::flat_hash_map<std::string, rapidjson::Value>&
        per_ig_priority_vectors) {
  absl::flat_hash_map<std::string, double> priorities_by_ig;

  for (const BuyerInputForBidding::InterestGroupForBidding& interest_group :
       buyer_input.interest_groups()) {
    PopulateDeviceSignalFields(priority_signals,
                               interest_group.browser_signals());
    const std::string& ig_name = interest_group.name();

    auto itr = per_ig_priority_vectors.find(ig_name);
    if (itr != per_ig_priority_vectors.end()) {
      priorities_by_ig[ig_name] =
          CalculateDotProduct(priority_signals, itr->second);
    } else {
      priorities_by_ig[ig_name] = 0;
    }
  }

  // Reset priority_signals to how it was when passed to this method.
  priority_signals.RemoveMember(kDeviceSignalsOne);
  priority_signals.RemoveMember(kDeviceSignalsAgeInMinutes);
  priority_signals.RemoveMember(kDeviceSignalsAgeInMinutesMax60);
  priority_signals.RemoveMember(kDeviceSignalsAgeInHoursMax24);
  priority_signals.RemoveMember(kDeviceSignalsAgeInDaysMax30);

  return priorities_by_ig;
}

}  // namespace privacy_sandbox::bidding_auction_servers
