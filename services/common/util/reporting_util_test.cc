//  Copyright 2023 Google LLC
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

#include "services/common/util/reporting_util.h"

#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "include/gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(GeneratePostAuctionSignalsTest, HasAllSignals) {
  std::optional<ScoreAdsResponse::AdScore> ad_score =
      std::make_optional(MakeARandomAdScore(2));
  std::unique_ptr<PostAuctionSignals> signals =
      GeneratePostAuctionSignals(ad_score);
  EXPECT_TRUE(ad_score->interest_group_owner() == signals->winning_ig_owner);
  EXPECT_TRUE(ad_score->interest_group_name() == signals->winning_ig_name);
  EXPECT_EQ(ad_score->buyer_bid(), signals->winning_bid);
  EXPECT_TRUE(signals->has_highest_scoring_other_bid);
  EXPECT_NE(signals->highest_scoring_other_bid_ig_owner, "");
  EXPECT_GT(signals->highest_scoring_other_bid, 0.0);
}

TEST(GeneratePostAuctionSignalsTest, DoesNotHaveHighestOtherBidSignals) {
  std::optional<ScoreAdsResponse::AdScore> ad_score =
      std::make_optional(MakeARandomAdScore(0));
  std::unique_ptr<PostAuctionSignals> signals =
      GeneratePostAuctionSignals(ad_score);
  EXPECT_TRUE(ad_score->interest_group_owner() == signals->winning_ig_owner);
  EXPECT_TRUE(ad_score->interest_group_name() == signals->winning_ig_name);
  EXPECT_EQ(ad_score->buyer_bid(), signals->winning_bid);
  EXPECT_FALSE(signals->has_highest_scoring_other_bid);
  EXPECT_EQ(signals->highest_scoring_other_bid_ig_owner, "");
  EXPECT_EQ(signals->highest_scoring_other_bid, 0.0);
}

TEST(GeneratePostAuctionSignalsTest, DoesNotHaveAdScore) {
  ScoreAdsResponse::AdScore ad_score = MakeARandomAdScore(0);
  std::unique_ptr<PostAuctionSignals> signals =
      GeneratePostAuctionSignals(std::nullopt);
  EXPECT_EQ("", signals->winning_ig_owner);
  EXPECT_EQ("", signals->winning_ig_name);
  EXPECT_EQ(0.0, signals->winning_bid);
  EXPECT_FALSE(signals->has_highest_scoring_other_bid);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithWinningBidSuccess) {
  absl::string_view url =
      "https://wikipedia.org?wb=${winningBid}&m_wb=${madeWinningBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(1.9, false);
  absl::string_view expected_url = "https://wikipedia.org?wb=1.9&m_wb=false";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);

  std::unique_ptr<DebugReportingPlaceholder> placeholder_2 =
      std::make_unique<DebugReportingPlaceholder>(1.9, true);
  expected_url = "https://wikipedia.org?wb=1.9&m_wb=true";
  request = CreateDebugReportingHttpRequest(url, std::move(placeholder_2));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithWinningBidAsZero) {
  absl::string_view url =
      "https://wikipedia.org?wb=${winningBid}&m_wb=${madeWinningBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(0.0, false);
  absl::string_view expected_url = "https://wikipedia.org?wb=0&m_wb=false";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithHighestOtherBidSuccess) {
  absl::string_view url =
      "https://"
      "wikipedia.org?hob=${highestScoringOtherBid}&m_hob=${"
      "madeHighestScoringOtherBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(1.9, false, 2.18, true);
  absl::string_view expected_url = "https://wikipedia.org?hob=2.18&m_hob=true";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);

  std::unique_ptr<DebugReportingPlaceholder> placeholder_2 =
      std::make_unique<DebugReportingPlaceholder>(1.9, false, 2.18, false);
  expected_url = "https://wikipedia.org?hob=2.18&m_hob=false";
  request = CreateDebugReportingHttpRequest(url, std::move(placeholder_2));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithHighestOtherBidAsZero) {
  absl::string_view url =
      "https://"
      "wikipedia.org?hob=${highestScoringOtherBid}&m_hob=${"
      "madeHighestScoringOtherBid}";
  std::unique_ptr<DebugReportingPlaceholder> placeholder_1 =
      std::make_unique<DebugReportingPlaceholder>(1.9, false);
  absl::string_view expected_url = "https://wikipedia.org?hob=0&m_hob=false";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder_1));
  EXPECT_EQ(request.url, expected_url);
}

TEST(CreateDebugReportingHttpRequestTest, GetWithNoPlaceholder) {
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      std::make_unique<DebugReportingPlaceholder>(1.9, false, 2.18, true);
  absl::string_view url = "https://wikipedia.org";
  absl::string_view expected_url = "https://wikipedia.org";
  HTTPRequest request =
      CreateDebugReportingHttpRequest(url, std::move(placeholder));
  EXPECT_EQ(request.url, expected_url);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerNone) {
  PostAuctionSignals signals;
  signals.winning_bid = 1.9;
  signals.winning_ig_name = "random-ig-name";
  signals.winning_ig_owner = "random_ig_owner";
  signals.has_highest_scoring_other_bid = false;
  absl::string_view test_ig_owner = "test_ig_owner";
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroupOwner(test_ig_owner, signals);
  EXPECT_FALSE(placeholder->made_winning_bid);
  EXPECT_FALSE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid, 0.0);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerIsWinner) {
  PostAuctionSignals signals;
  signals.winning_bid = 1.9;
  signals.winning_ig_name = "random-ig-name";
  signals.winning_ig_owner = "test_ig_owner";
  signals.has_highest_scoring_other_bid = false;
  absl::string_view test_ig_owner = "test_ig_owner";
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroupOwner(test_ig_owner, signals);
  EXPECT_TRUE(placeholder->made_winning_bid);
  EXPECT_FALSE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid, 0.0);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerMadeHighestOtherBid) {
  PostAuctionSignals signals;
  signals.winning_bid = 1.9;
  signals.winning_ig_name = "random-ig-name";
  signals.winning_ig_owner = "random_ig_owner";
  signals.has_highest_scoring_other_bid = true;
  signals.highest_scoring_other_bid_ig_owner = "test_ig_owner";
  signals.has_highest_scoring_other_bid = 2.18;
  absl::string_view test_ig_owner = "test_ig_owner";
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroupOwner(test_ig_owner, signals);
  EXPECT_FALSE(placeholder->made_winning_bid);
  EXPECT_TRUE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid,
            signals.highest_scoring_other_bid);
}

TEST(GetPlaceholderDataForInterestGroupOwnerTest, IgOwnerBoth) {
  PostAuctionSignals signals;
  signals.winning_bid = 1.9;
  signals.winning_ig_name = "random-ig-name";
  signals.winning_ig_owner = "test_ig_owner";
  signals.has_highest_scoring_other_bid = true;
  signals.highest_scoring_other_bid_ig_owner = "test_ig_owner";
  signals.has_highest_scoring_other_bid = 2.18;
  absl::string_view test_ig_owner = "test_ig_owner";
  std::unique_ptr<DebugReportingPlaceholder> placeholder =
      GetPlaceholderDataForInterestGroupOwner(test_ig_owner, signals);
  EXPECT_TRUE(placeholder->made_winning_bid);
  EXPECT_TRUE(placeholder->made_highest_scoring_other_bid);
  EXPECT_EQ(placeholder->winning_bid, signals.winning_bid);
  EXPECT_EQ(placeholder->highest_scoring_other_bid,
            signals.highest_scoring_other_bid);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
