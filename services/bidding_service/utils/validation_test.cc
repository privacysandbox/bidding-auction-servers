//  Copyright 2024 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/bidding_service/utils/validation.h"

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

AdWithBid MakeTestAdWithBid(float bid, bool allow_component_auction = true) {
  AdWithBid ad_with_bid = MakeARandomAdWithBid(1, 5);
  ad_with_bid.set_bid(bid);
  ad_with_bid.set_allow_component_auction(allow_component_auction);
  return ad_with_bid;
}

AdWithBid MakeTestAdWithBidWithDebugUrls(absl::string_view win_url,
                                         absl::string_view loss_url) {
  AdWithBid ad_with_bid = MakeARandomAdWithBid(1, 5);
  ad_with_bid.mutable_debug_report_urls()->set_auction_debug_win_url(win_url);
  ad_with_bid.mutable_debug_report_urls()->set_auction_debug_loss_url(loss_url);
  return ad_with_bid;
}

TEST(ValidateBuyerDebugUrls, HandlesNoDebugUrls) {
  AdWithBid ad_with_bid = MakeARandomAdWithBid(1, 5);
  ad_with_bid.clear_debug_report_urls();
  long current_total_debug_urls_chars = 5;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 5,
                              .max_allowed_size_all_debug_urls_chars = 10}),
      0);
  EXPECT_EQ(current_total_debug_urls_chars, 5);
}

TEST(ValidateBuyerDebugUrls, ClearsDebugUrlsIfTotalSizeAlreadyEqualsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 4,
                              .max_allowed_size_all_debug_urls_chars = 4}),
      0);
  EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(current_total_debug_urls_chars, 4);
}

TEST(ValidateBuyerDebugUrls, ClearsDebugWinUrlIfSizeExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("long_win", "loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 5,
                              .max_allowed_size_all_debug_urls_chars = 10}),
      1);
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
  EXPECT_EQ(current_total_debug_urls_chars, 8);
}

TEST(ValidateBuyerDebugUrls, ClearsDebugWinUrlIfNewTotalExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("long_win", "loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 10,
                              .max_allowed_size_all_debug_urls_chars = 10}),
      1);
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
  EXPECT_EQ(current_total_debug_urls_chars, 8);
}

TEST(ValidateBuyerDebugUrls, ClearsDebugLossUrlIfSizeExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "long_loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 5,
                              .max_allowed_size_all_debug_urls_chars = 10}),
      1);
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_loss_url().empty());
  EXPECT_EQ(current_total_debug_urls_chars, 7);
}

TEST(ValidateBuyerDebugUrls, ClearsDebugLossUrlIfNewTotalExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "long_loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 10,
                              .max_allowed_size_all_debug_urls_chars = 10}),
      1);
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_loss_url().empty());
  EXPECT_EQ(current_total_debug_urls_chars, 7);
}

TEST(ValidateBuyerDebugUrls, ClearsDebugUrlsIfBothWinAndLossExceedMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");
  long current_total_debug_urls_chars = 0;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 2,
                              .max_allowed_size_all_debug_urls_chars = 4}),
      0);
  EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(current_total_debug_urls_chars, 0);
}

TEST(ValidateBuyerDebugUrls, UpdatesTotalForValidDebugUrls) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(
      ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                             {.max_allowed_size_debug_url_chars = 5,
                              .max_allowed_size_all_debug_urls_chars = 15}),
      2);
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
  EXPECT_EQ(current_total_debug_urls_chars, 11);
}

TEST(ValidateBuyerDebugUrls, LossUrlFailsSamplingAndInvalidWinUrl) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("long_win", "loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                                   {.max_allowed_size_debug_url_chars = 5,
                                    .max_allowed_size_all_debug_urls_chars = 15,
                                    .enable_sampled_debug_reporting = true,
                                    .debug_reporting_sampling_upper_bound = 0}),
            0);
  EXPECT_FALSE(ad_with_bid.debug_win_url_failed_sampling());
  EXPECT_TRUE(ad_with_bid.debug_loss_url_failed_sampling());
  EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(current_total_debug_urls_chars, 4);
}

TEST(ValidateBuyerDebugUrls, WinUrlFailsSamplingAndInvalidLossUrl) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "long_loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                                   {.max_allowed_size_debug_url_chars = 5,
                                    .max_allowed_size_all_debug_urls_chars = 15,
                                    .enable_sampled_debug_reporting = true,
                                    .debug_reporting_sampling_upper_bound = 0}),
            0);
  EXPECT_TRUE(ad_with_bid.debug_win_url_failed_sampling());
  EXPECT_FALSE(ad_with_bid.debug_loss_url_failed_sampling());
  EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(current_total_debug_urls_chars, 4);
}

TEST(ValidateBuyerDebugUrls, BothWinAndLossUrlsAreValidAndPassSampling) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");
  long current_total_debug_urls_chars = 4;
  EXPECT_EQ(ValidateBuyerDebugUrls(ad_with_bid, current_total_debug_urls_chars,
                                   {.max_allowed_size_debug_url_chars = 5,
                                    .max_allowed_size_all_debug_urls_chars = 15,
                                    .enable_sampled_debug_reporting = true,
                                    .debug_reporting_sampling_upper_bound = 1}),
            2);
  EXPECT_FALSE(ad_with_bid.debug_win_url_failed_sampling());
  EXPECT_FALSE(ad_with_bid.debug_loss_url_failed_sampling());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
  EXPECT_EQ(current_total_debug_urls_chars, 11);
}

TEST(IsValidProtectedAudienceBid, ReturnsInvalidForZeroBid) {
  AdWithBid ad_with_bid = MakeTestAdWithBid(0);
  ASSERT_EQ(ad_with_bid.bid(), 0);

  EXPECT_EQ(IsValidProtectedAudienceBid(
                ad_with_bid, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER)
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(IsValidProtectedAudienceBid, ReturnsInvalidForRejectedComponentBid) {
  AdWithBid ad_with_bid = MakeTestAdWithBid(1, false);
  ASSERT_EQ(ad_with_bid.bid(), 1);
  ASSERT_FALSE(ad_with_bid.allow_component_auction());

  EXPECT_EQ(IsValidProtectedAudienceBid(
                ad_with_bid,
                AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER)
                .code(),
            absl::StatusCode::kPermissionDenied);
}

TEST(IsValidProtectedAudienceBid, ReturnsValidForProperBid) {
  AdWithBid ad_with_bid = MakeTestAdWithBid(1, true);

  EXPECT_TRUE(IsValidProtectedAudienceBid(
                  ad_with_bid,
                  AuctionScope::AUCTION_SCOPE_DEVICE_COMPONENT_MULTI_SELLER)
                  .ok());
}

TEST(IsValidProtectedAppSignalsBid, ReturnsInvalidForZeroBid) {
  ProtectedAppSignalsAdWithBid pas_ad_with_bid;
  pas_ad_with_bid.set_bid(0);
  ASSERT_EQ(pas_ad_with_bid.bid(), 0);

  EXPECT_EQ(IsValidProtectedAppSignalsBid(
                pas_ad_with_bid, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER)
                .code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(IsValidProtectedAppSignalsBid, ReturnsInvalidForRejectedComponentBid) {
  ProtectedAppSignalsAdWithBid pas_ad_with_bid;
  pas_ad_with_bid.set_bid(1.0);

  EXPECT_EQ(IsValidProtectedAppSignalsBid(
                pas_ad_with_bid,
                AuctionScope::AUCTION_SCOPE_SERVER_COMPONENT_MULTI_SELLER)
                .code(),
            absl::StatusCode::kPermissionDenied);
}

TEST(IsValidProtectedAppSignalsBid, ReturnsValidForProperBid) {
  ProtectedAppSignalsAdWithBid pas_ad_with_bid;
  pas_ad_with_bid.set_bid(1.0);

  EXPECT_TRUE(IsValidProtectedAppSignalsBid(
                  pas_ad_with_bid, AuctionScope::AUCTION_SCOPE_SINGLE_SELLER)
                  .ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
