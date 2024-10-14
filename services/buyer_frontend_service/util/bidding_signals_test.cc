// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/buyer_frontend_service/util/bidding_signals.h"

#include <include/gmock/gmock-matchers.h>

#include "api/bidding_auction_servers.pb.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_utils.h"
#include "services/common/util/json_util.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(ParseTrustedBiddingSignals, FailsForEmptyBiddingSignals) {
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>("");
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_FALSE(parsed_bidding_signals.ok());
  EXPECT_EQ(parsed_bidding_signals.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(parsed_bidding_signals.status().message(),
              testing::StrEq(kGetBiddingSignalsSuccessButEmpty));
}

TEST(ParseTrustedBiddingSignals, FailsForMalformedJsonBiddingSignals) {
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals =
      std::make_unique<std::string>("!not json!");
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_FALSE(parsed_bidding_signals.ok());
  EXPECT_EQ(parsed_bidding_signals.status().code(),
            absl::StatusCode::kInternal);
  EXPECT_THAT(parsed_bidding_signals.status().message(),
              testing::StrEq(kBiddingSignalsJsonNotParseable));
}

TEST(ParseTrustedBiddingSignals, FailsForMissingKeysObjectProperty) {
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals =
      std::make_unique<std::string>("{\"keys\": 1}");
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_FALSE(parsed_bidding_signals.ok());
  EXPECT_EQ(parsed_bidding_signals.status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(parsed_bidding_signals.status().message(),
              testing::StrEq(kBiddingSignalsJsonMissingKeysProperty));
}

TEST(ParseTrustedBiddingSignals, SuccessfullyParsesWellFormedBiddingSignals) {
  auto input_ig = MakeARandomInterestGroupFromBrowser();
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      MakeBiddingSignalsForIGFromDevice(*input_ig.get()));
  auto parsed_bidding_signals = ParseTrustedBiddingSignals(
      std::move(bidding_signals), /*buyer_input*/ {});
  EXPECT_TRUE(parsed_bidding_signals.ok());
}

TEST(ParsePerInterestGroupData, SkipsNonObjectPerInterestGroupData) {
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      R"JSON(
{
    "keys": {},
    "perInterestGroupData": 123
}
    )JSON");
  BuyerInput input;
  auto output = ParseTrustedBiddingSignals(std::move(bidding_signals), input);
  EXPECT_TRUE(output.ok());
  EXPECT_TRUE(output->update_igs.interest_groups().empty());
}

TEST(ParsePerInterestGroupData,
     ParsesUpdateInterestGroupDataInBuyerInputOrder) {
  BuyerInput input;
  BuyerInput::InterestGroup input_ig_first;
  input_ig_first.set_name("first");
  BuyerInput::InterestGroup input_ig_second;
  input_ig_second.set_name("second");
  *input.add_interest_groups() = std::move(input_ig_first);
  *input.add_interest_groups() = std::move(input_ig_second);
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      R"JSON(
{
    "keys": {},
    "perInterestGroupData": {
        "first": {
            "priorityVector": {},
            "updateIfOlderThanMs": 100
        },
        "second": {
            "priorityVector": {},
            "updateIfOlderThanMs": 200
        }
    }
}
  )JSON");

  auto output = ParseTrustedBiddingSignals(std::move(bidding_signals), input);

  EXPECT_TRUE(output.ok());
  EXPECT_EQ(output->update_igs.interest_groups()[0].index(), 0);
  EXPECT_EQ(output->update_igs.interest_groups()[1].index(), 1);
  EXPECT_EQ(output->update_igs.interest_groups()[0].update_if_older_than_ms(),
            100);
  EXPECT_EQ(output->update_igs.interest_groups()[1].update_if_older_than_ms(),
            200);
}

TEST(ParsePerInterestGroupData, SkipBadUpdateIfOlderThanMsValue) {
  BuyerInput input;
  BuyerInput::InterestGroup input_ig_first;
  input_ig_first.set_name("first");
  *input.add_interest_groups() = std::move(input_ig_first);
  auto bidding_signals = std::make_unique<BiddingSignals>();
  bidding_signals->trusted_signals = std::make_unique<std::string>(
      R"JSON(
{
    "keys": {},
    "perInterestGroupData": {
        "first": {
            "priorityVector": {},
            "updateIfOlderThanMs": -1
        }
    }
}
  )JSON");

  auto parsed_bidding_signals =
      ParseTrustedBiddingSignals(std::move(bidding_signals), input);
  EXPECT_TRUE(parsed_bidding_signals.ok());
  EXPECT_TRUE(parsed_bidding_signals->update_igs.interest_groups().empty());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
