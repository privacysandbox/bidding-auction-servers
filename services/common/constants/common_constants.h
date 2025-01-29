/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_CONSTANTS_COMMON_CONSTANTS_H_
#define SERVICES_COMMON_CONSTANTS_COMMON_CONSTANTS_H_

#include <string>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers {

inline constexpr absl::string_view kIgnoredPlaceholderValue = "PLACEHOLDER";
inline constexpr absl::string_view kBiddingAuctionCompressionHeader =
    "bidding-auction-compression-type";
inline constexpr absl::string_view kSignalsNotFetched = "NOT_FETCHED";
inline constexpr absl::string_view kSignalsFetchedButOptional =
    "FETCHED_BUT_OPTIONAL";
inline constexpr absl::string_view kSignalsRequired = "REQUIRED";
inline constexpr char kDataVersion[] = "dataVersion";
inline constexpr char kKvRenderUrls[] = "renderUrls";
inline constexpr char kKvAdComponentRenderUrls[] = "adComponentRenderUrls";
inline constexpr char kKvExperimentGroupId[] = "experiment_group_id";
// TODO(b/379335128): Change to "TimeoutDuration" and verify no perf regression.
inline constexpr char kRomaTimeoutTag[] = "IGNORED";

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CONSTANTS_COMMON_CONSTANTS_H_
