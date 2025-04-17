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

#include "services/common/chaffing/moving_median_manager.h"

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

inline constexpr absl::string_view kBuyerNotPresentError =
    "Buyer not present in moving median manager: ";

}  // namespace

MovingMedianManager::MovingMedianManager(
    const absl::flat_hash_set<std::string>& buyers, size_t window_size,
    float sampling_probability) {
  for (const std::string& buyer : buyers) {
    moving_medians_by_buyer_.emplace(
        buyer, MovingMedian(window_size, sampling_probability));
  }
}

absl::Status MovingMedianManager::AddNumberToBuyerWindow(
    absl::string_view buyer, RandomNumberGenerator& rng, int val) {
  auto it = moving_medians_by_buyer_.find(buyer);
  if (it == moving_medians_by_buyer_.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kBuyerNotPresentError, buyer));
  }

  it->second.AddNumber(rng, val);
  return absl::OkStatus();
}

absl::StatusOr<int> MovingMedianManager::GetMedian(
    absl::string_view buyer) const {
  auto it = moving_medians_by_buyer_.find(buyer);
  if (it == moving_medians_by_buyer_.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kBuyerNotPresentError, buyer));
  }

  return it->second.GetMedian();
}

absl::StatusOr<bool> MovingMedianManager::IsWindowFilled(
    absl::string_view buyer) const {
  auto it = moving_medians_by_buyer_.find(buyer);
  if (it == moving_medians_by_buyer_.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kBuyerNotPresentError, buyer));
  }

  return it->second.IsWindowFilled();
}

}  // namespace privacy_sandbox::bidding_auction_servers
