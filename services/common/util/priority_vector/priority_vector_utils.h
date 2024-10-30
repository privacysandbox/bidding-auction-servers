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

#ifndef SERVICES_COMMON_UTIL_PRIORITY_VECTOR_PRIORITY_VECTOR_UTILS_H_
#define SERVICES_COMMON_UTIL_PRIORITY_VECTOR_PRIORITY_VECTOR_UTILS_H_

#include <string>

#include "absl/status/statusor.h"
#include "api/bidding_auction_servers.pb.h"
#include "rapidjson/document.h"

namespace privacy_sandbox::bidding_auction_servers {

// Parses a priority vector from a JSON string.
// Any entries where the key is not a string or the value is not a number are
// dropped.
absl::StatusOr<rapidjson::Document> ParsePriorityVector(
    const std::string& priority_vector_json);

// Merges the priority signal overrides in the per_buyer_config for a particular
// buyer into (a copy of) the provided priority_signals document. Returns the
// JSON-ified string result of the merge.
absl::StatusOr<std::string> GetBuyerPrioritySignals(
    const rapidjson::Document& priority_signals,
    const google::protobuf::Map<std::string,
                                SelectAdRequest::AuctionConfig::PerBuyerConfig>&
        per_buyer_config,
    const std::string& buyer_ig_owner);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_UTIL_PRIORITY_VECTOR_PRIORITY_VECTOR_UTILS_H_
