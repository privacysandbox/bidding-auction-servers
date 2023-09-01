// Copyright 2023 Google LLC
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

#include "services/common/util/bid_stats.h"

#include <utility>

#include "absl/strings/str_cat.h"
#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {

BidStats::BidStats(int initial_bids_count_input, const ContextLogger& logger,
                   absl::AnyInvocable<void(bool) &&> on_all_bids_done)
    : initial_bids_count_(initial_bids_count_input),
      pending_bids_count_(initial_bids_count_input),
      successful_bids_count_(0),
      empty_bids_count_(0),
      skipped_bids_count_(0),
      error_bids_count_(0),
      on_all_bids_done_(std::move(on_all_bids_done)),
      logger_(logger) {}

void BidStats::BidCompleted(CompletedBidState completed_bid_state) {
  BidCompleted(completed_bid_state, std::nullopt);
}

void BidStats::BidCompleted(
    CompletedBidState completed_bid_state,
    std::optional<absl::AnyInvocable<void()>> on_single_bid_done) {
  bool is_last_response = false;
  bool any_successful_bids = false;

  {
    absl::MutexLock lock(&mu_);
    DCHECK(pending_bids_count_ > 0)
        << "Unexpected call (indicates either a bug in the initialization or "
           "the usage)";

    if (on_single_bid_done.has_value()) {
      (*on_single_bid_done)();
    }

    pending_bids_count_--;
    switch (completed_bid_state) {
      case CompletedBidState::ERROR:
        error_bids_count_++;
        break;
      case CompletedBidState::EMPTY_RESPONSE:
        empty_bids_count_++;
        break;
      case CompletedBidState::SKIPPED:
        skipped_bids_count_++;
        break;
      case CompletedBidState::SUCCESS:
        successful_bids_count_++;
        break;
      default:
        // This indicates a bug in our logic.
        break;
    }
    logger_.vlog(5, "Updated pending bids state: ", ToString());
    if (pending_bids_count_ == 0) {
      is_last_response = true;
      // Accounts for chaffs.
      any_successful_bids = HasAnySuccessfulBids();
    }
  }

  // Must be called without holding lock.
  if (is_last_response) {
    std::move(on_all_bids_done_)(any_successful_bids);
  }
}

bool BidStats::HasAnySuccessfulBids() {
  const int sum_bids_count = skipped_bids_count_ + successful_bids_count_ +
                             error_bids_count_ + empty_bids_count_ +
                             pending_bids_count_;
  DCHECK_EQ(initial_bids_count_, sum_bids_count);

  const bool possible_chaff = empty_bids_count_ > 0 || skipped_bids_count_ > 0;
  return successful_bids_count_ > 0 || possible_chaff || error_bids_count_ == 0;
}

std::string BidStats::ToString() {
  return absl::StrCat("Bidding Stats: succeeded=", successful_bids_count_,
                      ", errored=", error_bids_count_,
                      ", skipped=", skipped_bids_count_,
                      ", returned empty=", empty_bids_count_,
                      ", pending=", pending_bids_count_);
}

}  // namespace privacy_sandbox::bidding_auction_servers
