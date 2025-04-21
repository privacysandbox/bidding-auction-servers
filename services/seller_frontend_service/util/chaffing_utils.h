/*
 * Copyright 2025 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_

#include <random>
#include <vector>

#include "absl/strings/string_view.h"
#include "services/common/chaffing/moving_median_manager.h"
#include "services/common/clients/async_grpc/request_config.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/util/status_utils.h"

namespace privacy_sandbox::bidding_auction_servers {

// Chaffing V1 constants.
inline constexpr int kMinChaffRequestSizeBytes = 9'000;
inline constexpr int kMaxChaffRequestSizeBytes = 95'000;

// Chaffing V2 constants.
inline constexpr size_t kChaffingV2MovingMedianWindowSize = 10'000;
inline constexpr int kChaffingV2UnfilledWindowRequestSizeBytes = 100'000;
inline constexpr float kChaffingV2SamplingProbablility = 0.1;

struct InvokedBuyers {
  std::vector<absl::string_view> chaff_buyer_names;
  std::vector<absl::string_view> non_chaff_buyer_names;
};

RequestConfig GetChaffingV1GetBidsRequestConfig(bool is_chaff_request,
                                                RandomNumberGenerator& rng);

RequestConfig GetChaffingV2GetBidsRequestConfig(
    absl::string_view buyer_name, bool is_chaff_request,
    const MovingMedianManager& moving_median_manager, RequestContext context);

// Returns whether chaffing should be skipped based on the provided RNG.
bool ShouldSkipChaffing(size_t num_buyers, RandomNumberGenerator& rng);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_UTIL_CHAFFING_UTILS_H_
