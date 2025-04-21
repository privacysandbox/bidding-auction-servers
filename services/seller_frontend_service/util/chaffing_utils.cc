// Copyright 2025 Google LLC
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

#include "services/seller_frontend_service/util/chaffing_utils.h"

#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

RequestConfig GetChaffingV1GetBidsRequestConfig(bool is_chaff_request,
                                                RandomNumberGenerator& rng) {
  RequestConfig request_config = {.minimum_request_size = 0,
                                  .compression_type = CompressionType::kGzip,
                                  .is_chaff_request = is_chaff_request};

  if (is_chaff_request) {
    request_config.minimum_request_size =
        rng.GetUniformInt(kMinChaffRequestSizeBytes, kMaxChaffRequestSizeBytes);
  }

  return request_config;
}

RequestConfig GetChaffingV2GetBidsRequestConfig(
    absl::string_view buyer_name, bool is_chaff_request,
    const MovingMedianManager& moving_median_manager, RequestContext context) {
  RequestConfig request_config = {.minimum_request_size = 0,
                                  .compression_type = CompressionType::kGzip,
                                  .is_chaff_request = is_chaff_request};

  if (is_chaff_request) {
    absl::StatusOr<bool> is_window_filled =
        moving_median_manager.IsWindowFilled(buyer_name);
    absl::StatusOr<int> median = moving_median_manager.GetMedian(buyer_name);
    if (!is_window_filled.ok() || !median.ok()) {
      PS_LOG(ERROR, context.log)
          << FirstNotOkStatusOr(is_window_filled, median);
      request_config.minimum_request_size =
          kChaffingV2UnfilledWindowRequestSizeBytes;
    } else {
      request_config.minimum_request_size =
          *is_window_filled ? *median
                            : kChaffingV2UnfilledWindowRequestSizeBytes;
    }
  } else {
    // Non-chaff request - pad the request if the per buyer window is *not*
    // filled.
    absl::StatusOr<bool> is_window_filled =
        moving_median_manager.IsWindowFilled(buyer_name);
    if (!is_window_filled.ok()) {
      PS_LOG(ERROR, context.log) << is_window_filled.status();
      request_config.minimum_request_size =
          kChaffingV2UnfilledWindowRequestSizeBytes;
    } else {
      request_config.minimum_request_size =
          *is_window_filled ? 0 : kChaffingV2UnfilledWindowRequestSizeBytes;
    }
  }

  return request_config;
}

bool ShouldSkipChaffing(size_t num_buyers, RandomNumberGenerator& rng) {
  if (num_buyers <= 5) {
    // Skip chaffing for 99% of requests.
    return (rng.GetUniformReal(0, 1) < .99);
  }

  // Skip chaffing for 95% of requests.
  return (rng.GetUniformReal(0, 1) < .95);
}

}  // namespace privacy_sandbox::bidding_auction_servers
