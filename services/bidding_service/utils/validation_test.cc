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

TEST(TrimAndReturnDebugUrlsSize, HandlesEmptyDebugUrls) {
  AdWithBid ad_with_bid = MakeARandomAdWithBid(1, 5);
  ad_with_bid.clear_debug_report_urls();
  ASSERT_FALSE(ad_with_bid.has_debug_report_urls());

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid, /*max_allowed_size_debug_url_chars=*/5,
                /*max_allowed_size_all_debug_urls_chars=*/10,
                /*total_debug_urls_chars=*/5, NoOpContext().log),
            0);
}

TEST(TrimAndReturnDebugUrlsSize, ClearsDebugUrlsIfTotalSizeAlreadyEqualsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/4,
                /*max_allowed_size_all_debug_urls_chars=*/4,
                /*total_debug_urls_chars=*/4, NoOpContext().log),
            0);
  EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
}

TEST(TrimAndReturnDebugUrlsSize, ClearsDebugWinUrlIfSizeExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("long_win", "loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/5,
                /*max_allowed_size_all_debug_urls_chars=*/10,
                /*total_debug_urls_chars=*/4, NoOpContext().log),
            4);
  EXPECT_TRUE(ad_with_bid.has_debug_report_urls());
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
}

TEST(TrimAndReturnDebugUrlsSize, ClearsDebugWinUrlIfNewTotalExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("long_win", "loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/10,
                /*max_allowed_size_all_debug_urls_chars=*/10,
                /*total_debug_urls_chars=*/4, NoOpContext().log),
            4);
  EXPECT_TRUE(ad_with_bid.has_debug_report_urls());
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_win_url().empty());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
}

TEST(TrimAndReturnDebugUrlsSize, ClearsDebugLossUrlIfSizeExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "long_loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/5,
                /*max_allowed_size_all_debug_urls_chars=*/10,
                /*total_debug_urls_chars=*/4, NoOpContext().log),
            3);
  EXPECT_TRUE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_loss_url().empty());
}

TEST(TrimAndReturnDebugUrlsSize, ClearsDebugLossUrlIfNewTotalExceedsMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "long_loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/10,
                /*max_allowed_size_all_debug_urls_chars=*/10,
                /*total_debug_urls_chars=*/4, NoOpContext().log),
            3);
  EXPECT_TRUE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_TRUE(ad_with_bid.debug_report_urls().auction_debug_loss_url().empty());
}

TEST(TrimAndReturnDebugUrlSize, ClearsDebugUrlsIfBothWinAndLossExceedMax) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/2,
                /*max_allowed_size_all_debug_urls_chars=*/4,
                /*total_debug_urls_chars=*/0, NoOpContext().log),
            0);
  EXPECT_FALSE(ad_with_bid.has_debug_report_urls());
}

TEST(TrimAndReturnDebugUrlsSize, UpdatesTotalForValidDebugUrls) {
  AdWithBid ad_with_bid = MakeTestAdWithBidWithDebugUrls("win", "loss");

  EXPECT_EQ(TrimAndReturnDebugUrlsSize(
                ad_with_bid,
                /*max_allowed_size_debug_url_chars=*/5,
                /*max_allowed_size_all_debug_urls_chars=*/15,
                /*total_debug_urls_chars=*/4, NoOpContext().log),
            7);
  EXPECT_TRUE(ad_with_bid.has_debug_report_urls());
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_win_url(), "win");
  EXPECT_EQ(ad_with_bid.debug_report_urls().auction_debug_loss_url(), "loss");
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

  EXPECT_EQ(IsValidProtectedAppSignalsBid(pas_ad_with_bid).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(IsValidProtectedAppSignalsBid, ReturnsValidForProperBid) {
  ProtectedAppSignalsAdWithBid pas_ad_with_bid;
  pas_ad_with_bid.set_bid(1.0);

  EXPECT_TRUE(IsValidProtectedAppSignalsBid(pas_ad_with_bid).ok());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
