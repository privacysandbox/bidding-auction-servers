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

#ifndef SERVICES_BIDDING_SERVICE_UTILS_VALIDATION_H_
#define SERVICES_BIDDING_SERVICE_UTILS_VALIDATION_H_

#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "api/bidding_auction_servers.pb.h"
#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {

// Clears debug URLs for ad_with_bid if they exceed the maximum allowed size per
// debug URL, or if they cause the total size of all debug URLs to exceed the
// maximum allowed size of all debug URLs. Returns updated size of debug URLs of
// ad_with_bid.
long TrimAndReturnDebugUrlsSize(AdWithBid& ad_with_bid,
                                int max_allowed_size_debug_url_chars,
                                long max_allowed_size_all_debug_urls_chars,
                                long total_debug_urls_chars,
                                RequestLogContext& log_context);

// Converts protected audience bid to string for logging.
inline std::string GetProtectedAudienceBidDebugInfo(const AdWithBid& bid) {
  return absl::StrCat(bid.interest_group_name(), ": ", bid.DebugString());
}

// Validates protected audience bid and returns reason if deemed invalid.
absl::Status IsValidProtectedAudienceBid(const AdWithBid& bid,
                                         AuctionScope auction_scope);

// Gets string information about a protected app signals bid's well-formed-ness.
inline std::string GetProtectedAppSignalsBidDebugInfo(
    const ProtectedAppSignalsAdWithBid& bid) {
  return absl::StrCat(
      "Protected App Signals bid (Is non-zero bid: ", bid.bid() > 0.0f,
      ", Num egress bytes: ", bid.egress_payload().size(),
      ", Has debug report urls: ", bid.has_debug_report_urls(), ")");
}

// Validates protected app signals bid and returns reason if deemed invalid.
absl::Status IsValidProtectedAppSignalsBid(
    const ProtectedAppSignalsAdWithBid& bid);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_UTILS_VALIDATION_H_
