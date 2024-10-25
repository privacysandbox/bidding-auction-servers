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

#include <utility>

#include "services/common/loggers/request_log_context.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<rapidjson::Document> ParsePriorityVector(
    const std::string& priority_vector_json) {
  absl::StatusOr<rapidjson::Document> priority_vector =
      ParseJsonString(priority_vector_json);
  if (!priority_vector.ok() || !priority_vector->IsObject()) {
    return absl::InvalidArgumentError("Malformed priority vector JSON");
  }

  for (auto itr = priority_vector->MemberBegin();
       itr != priority_vector->MemberEnd();) {
    if (!itr->name.IsString() || !itr->value.IsNumber()) {
      itr = priority_vector->EraseMember(itr);
    } else {
      ++itr;
    }
  }

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
    if (!buyer_priority_signals.HasMember(itr->name)) {
      // Key doesn't exist, so add it.
      buyer_priority_signals.AddMember(itr->name, value,
                                       buyer_priority_signals.GetAllocator());
    } else {
      // Key exists, so overwrite the value.
      buyer_priority_signals[itr->name.GetString()].SetDouble(value);
    }
  }

  return SerializeJsonDoc(buyer_priority_signals);
}

}  // namespace privacy_sandbox::bidding_auction_servers
