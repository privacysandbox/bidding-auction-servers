// Copyright 2024 Google LLC
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

#include "services/bidding_service/utils/validation.h"

namespace privacy_sandbox::bidding_auction_servers {

DebugUrlsSize TrimAndReturnDebugUrlsSize(
    AdWithBid& ad_with_bid, int max_allowed_size_debug_url_chars,
    long max_allowed_size_all_debug_urls_chars, long total_debug_urls_chars,
    RequestLogContext& log_context) {
  // No debug URLs present.
  if (!ad_with_bid.has_debug_report_urls()) {
    return {.win_url_chars = 0, .loss_url_chars = 0};
  }
  // Size of existing debug URLs for this bid.
  long win_url_chars =
      ad_with_bid.debug_report_urls().auction_debug_win_url().length();
  long loss_url_chars =
      ad_with_bid.debug_report_urls().auction_debug_loss_url().length();
  // Clear debug URLs for this bid since current size of all debug URLs already
  // exceeds maximum allowed size of all debug URLs.
  if (total_debug_urls_chars >= max_allowed_size_all_debug_urls_chars) {
    if (win_url_chars > 0) {
      PS_VLOG(kNoisyWarn, log_context)
          << "Skipped debug win URL for " << ad_with_bid.interest_group_name()
          << ": " << ad_with_bid.debug_report_urls().auction_debug_win_url();
    }
    if (loss_url_chars > 0) {
      PS_VLOG(kNoisyWarn, log_context)
          << "Skipped debug loss URL for " << ad_with_bid.interest_group_name()
          << ": " << ad_with_bid.debug_report_urls().auction_debug_loss_url();
    }
    ad_with_bid.clear_debug_report_urls();
    return {.win_url_chars = 0, .loss_url_chars = 0};
  }
  // Clear win debug URL if:
  // i) it exceeds maximum allowed size per debug URL, or
  // ii) it causes total size of all debug URLs to exceed maximum allowed size
  // of all debug URLs.
  if (win_url_chars > max_allowed_size_debug_url_chars ||
      win_url_chars + total_debug_urls_chars >
          max_allowed_size_all_debug_urls_chars) {
    PS_VLOG(kNoisyWarn, log_context)
        << "Skipped debug win URL for " << ad_with_bid.interest_group_name()
        << ": " << ad_with_bid.debug_report_urls().auction_debug_win_url();
    ad_with_bid.mutable_debug_report_urls()->clear_auction_debug_win_url();
    win_url_chars = 0;
  } else {
    total_debug_urls_chars += win_url_chars;
  }
  // Clear loss debug URL if:
  // i) it exceeds maximum allowed size per debug URL, or
  // ii) it causes total size of all debug URLs to exceed maximum allowed size
  // of all debug URLs.
  if (loss_url_chars > max_allowed_size_debug_url_chars ||
      loss_url_chars + total_debug_urls_chars >
          max_allowed_size_all_debug_urls_chars) {
    PS_VLOG(kNoisyWarn, log_context)
        << "Skipped debug loss URL for " << ad_with_bid.interest_group_name()
        << ": " << ad_with_bid.debug_report_urls().auction_debug_loss_url();
    ad_with_bid.mutable_debug_report_urls()->clear_auction_debug_loss_url();
    loss_url_chars = 0;
  }
  if (win_url_chars == 0 && loss_url_chars == 0) {
    ad_with_bid.clear_debug_report_urls();
  }
  return {.win_url_chars = win_url_chars, .loss_url_chars = loss_url_chars};
}

absl::Status IsValidProtectedAudienceBid(const AdWithBid& bid,
                                         AuctionScope auction_scope) {
  // Zero bid
  if (bid.bid() == 0.0f) {
    return absl::InvalidArgumentError(
        absl::StrCat("Zero bid will be ignored for ",
                     GetProtectedAudienceBidDebugInfo(bid)));
  }
  // Is a component auction but bid does not allow component auctions
  if (auction_scope ==
          AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER &&
      !bid.allow_component_auction()) {
    return absl::PermissionDeniedError(
        absl::StrCat("Component bid is not allowed for ",
                     GetProtectedAudienceBidDebugInfo(bid)));
  }
  return absl::OkStatus();
}

absl::Status IsValidProtectedAppSignalsBid(
    const ProtectedAppSignalsAdWithBid& bid, AuctionScope auction_scope) {
  // Zero bid
  if (bid.bid() == 0.0f) {
    return absl::InvalidArgumentError(
        absl::StrCat("Zero bid will be ignored for ",
                     GetProtectedAppSignalsBidDebugInfo(bid)));
  }

  // Is a component auction but bid does not allow component auctions
  if (auction_scope ==
          AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER &&
      !bid.allow_component_auction()) {
    return absl::PermissionDeniedError(
        absl::StrCat("Component bid is not allowed for ",
                     GetProtectedAppSignalsBidDebugInfo(bid)));
  }
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
