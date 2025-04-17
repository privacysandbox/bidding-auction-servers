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
namespace {

// Logs and updates metric for a rejected debug url.
void LogRejectedDebugUrl(absl::string_view url, absl::string_view reject_reason,
                         absl::string_view interest_group_name,
                         RequestLogContext& log_context,
                         metric::BiddingContext* metric_context) {
  if (url.empty()) {
    return;
  }
  PS_VLOG(8, log_context) << "Skipped debug url for " << interest_group_name
                          << ": " << url;
  if (metric_context) {
    LogIfError(metric_context->AccumulateMetric<metric::kBiddingDebugUrlCount>(
        1, reject_reason));
  }
}

// Performs checks for max size, max total size, and sampling for a given debug
// url. Updates current_url_chars and/or current_total_debug_urls_chars.
// Returns the rejection reason if the url does not pass validation checks.
std::optional<absl::string_view> GetRejectReasonIfBuyerDebugUrlInvalid(
    long& current_url_chars, long& current_total_debug_urls_chars,
    const DebugUrlsValidationConfig& config) {
  if (current_url_chars == 0) {
    return std::nullopt;
  }
  if (current_url_chars > config.max_allowed_size_debug_url_chars) {
    current_url_chars = 0;
    return kDebugUrlRejectedForExceedingSize;
  }
  if (current_url_chars + current_total_debug_urls_chars >
      config.max_allowed_size_all_debug_urls_chars) {
    current_url_chars = 0;
    return kDebugUrlRejectedForExceedingTotalSize;
  }
  if (config.enable_sampled_debug_reporting &&
      !DebugUrlPassesSampling(config.debug_reporting_sampling_upper_bound)) {
    current_url_chars = 0;
    return kDebugUrlRejectedDuringSampling;
  }
  current_total_debug_urls_chars += current_url_chars;
  return std::nullopt;
}

}  // namespace

int ValidateBuyerDebugUrls(AdWithBid& ad_with_bid,
                           long& current_total_debug_urls_chars,
                           const DebugUrlsValidationConfig& config,
                           RequestLogContext& log_context,
                           metric::BiddingContext* metric_context) {
  // No debug URLs present.
  if (!ad_with_bid.has_debug_report_urls()) {
    return 0;
  }

  // Size of existing debug urls for this bid.
  absl::string_view ig_name = ad_with_bid.interest_group_name();
  auto* debug_urls = ad_with_bid.mutable_debug_report_urls();
  long win_url_chars = debug_urls->auction_debug_win_url().length();
  long loss_url_chars = debug_urls->auction_debug_loss_url().length();

  // Early exit: Total size limit already reached before this bid.
  if (current_total_debug_urls_chars >=
      config.max_allowed_size_all_debug_urls_chars) {
    LogRejectedDebugUrl(debug_urls->auction_debug_win_url(),
                        kDebugUrlRejectedForExceedingTotalSize, ig_name,
                        log_context, metric_context);
    LogRejectedDebugUrl(debug_urls->auction_debug_loss_url(),
                        kDebugUrlRejectedForExceedingTotalSize, ig_name,
                        log_context, metric_context);
    ad_with_bid.clear_debug_report_urls();
    return 0;
  }

  // Validate debug win url.
  std::optional<absl::string_view> reject_reason;
  if (reject_reason = GetRejectReasonIfBuyerDebugUrlInvalid(
          win_url_chars, current_total_debug_urls_chars, config);
      reject_reason.has_value()) {
    LogRejectedDebugUrl(debug_urls->auction_debug_win_url(), *reject_reason,
                        ig_name, log_context, metric_context);
    debug_urls->clear_auction_debug_win_url();
    ad_with_bid.set_debug_win_url_failed_sampling(
        *reject_reason == kDebugUrlRejectedDuringSampling);
  }

  // Validate debug loss url.
  if (reject_reason = GetRejectReasonIfBuyerDebugUrlInvalid(
          loss_url_chars, current_total_debug_urls_chars, config);
      reject_reason.has_value()) {
    LogRejectedDebugUrl(debug_urls->auction_debug_loss_url(), *reject_reason,
                        ig_name, log_context, metric_context);
    debug_urls->clear_auction_debug_loss_url();
    ad_with_bid.set_debug_loss_url_failed_sampling(
        *reject_reason == kDebugUrlRejectedDuringSampling);
  }

  // Cleanup: If both urls were cleared, remove the container.
  if (win_url_chars == 0 && loss_url_chars == 0) {
    ad_with_bid.clear_debug_report_urls();
  }

  // Return count of non-empty validated debug urls.
  return (win_url_chars > 0) + (loss_url_chars > 0);
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
