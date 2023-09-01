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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_BID_STATS_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_BID_STATS_H_

#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "services/common/util/context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

enum class CompletedBidState {
  UNKNOWN,
  SKIPPED,         // Missing data related to the buyer, hence BFE call skipped.
  EMPTY_RESPONSE,  // Buyer returned no response for the GetBid call.
  ERROR,
  SUCCESS,
};

// Helper class that facilitates the clients to wait for all bids to be
// completed before executing the registered callback. Clients need to ensure
// that the provided logger outlives instance of `BidStats` class.
class BidStats {
 public:
  explicit BidStats(int initial_bids_count_input, const ContextLogger& logger,
                    absl::AnyInvocable<void(bool) &&> on_all_bids_done);

  // Updates the stats. If all bids have been completed, then the registered
  // callback is called.
  void BidCompleted(CompletedBidState completed_bid_state)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Updates the stats. If all bids have been completed, then the registered
  // callback is called. `on_single_bid_done`, if provided, will be called with
  // a lock.
  void BidCompleted(
      CompletedBidState completed_bid_state,
      std::optional<absl::AnyInvocable<void()>> on_single_bid_done)
      ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Indicates whether any bid was successful or if no buyer returned an empty
  // bid so that we should send a chaff back. This should be called after all
  // the get bid calls to buyer frontend have returned.
  bool HasAnySuccessfulBids() ABSL_GUARDED_BY(mu_);

  std::string ToString() ABSL_GUARDED_BY(mu_);

  const int initial_bids_count_;
  absl::Mutex mu_;
  int pending_bids_count_ ABSL_GUARDED_BY(mu_);
  int successful_bids_count_ ABSL_GUARDED_BY(mu_);
  int empty_bids_count_ ABSL_GUARDED_BY(mu_);
  int skipped_bids_count_ ABSL_GUARDED_BY(mu_);
  int error_bids_count_ ABSL_GUARDED_BY(mu_);
  absl::AnyInvocable<void(bool) &&> on_all_bids_done_ ABSL_LOCKS_EXCLUDED(mu_);
  const ContextLogger& logger_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_BID_STATS_H_
