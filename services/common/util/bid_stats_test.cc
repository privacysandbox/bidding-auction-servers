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

#include "services/common/util/bid_stats.h"

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "include/gtest/gtest.h"
#include "services/common/util/context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr int kNumMaxThreads = 10;

class BidStatsTest : public testing::Test {
 protected:
  ContextLogger context_logger_;
  absl::Notification notification_;
};

TEST_F(BidStatsTest, CallsBackOnBiddingSuccess) {
  BidStats bid_stats(1, context_logger_, [this](bool any_successful) {
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::thread t(
      [&bid_stats]() { bid_stats.BidCompleted(CompletedBidState::SUCCESS); });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(BidStatsTest, CallsBackOnBiddingError) {
  BidStats bid_stats(1, context_logger_, [this](bool any_successful) {
    EXPECT_FALSE(any_successful);
    notification_.Notify();
  });

  std::thread t(
      [&bid_stats]() { bid_stats.BidCompleted(CompletedBidState::ERROR); });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(BidStatsTest, CallsBackOnBiddingResultSkipped) {
  BidStats bid_stats(1, context_logger_, [this](bool any_successful) {
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::thread t(
      [&bid_stats]() { bid_stats.BidCompleted(CompletedBidState::SKIPPED); });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(BidStatsTest, CallsBackOnBiddingResultEmptyResponse) {
  BidStats bid_stats(1, context_logger_, [this](bool any_successful) {
    // No bid errored and one bid successful => chaff to the caller.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::thread t([&bid_stats]() {
    bid_stats.BidCompleted(CompletedBidState::EMPTY_RESPONSE);
  });

  notification_.WaitForNotification();
  t.join();
}

TEST_F(BidStatsTest, DeclaresSuccessIfAnyBidSucceeded) {
  BidStats bid_stats(4, context_logger_, [this](bool any_successful) {
    // At least one bid succeeded => succeeded overall.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::vector<std::thread> threads;
  for (auto status :
       {CompletedBidState::SUCCESS, CompletedBidState::ERROR,
        CompletedBidState::SKIPPED, CompletedBidState::EMPTY_RESPONSE}) {
    threads.emplace_back(
        [&bid_stats, status]() { bid_stats.BidCompleted(status); });
  }

  notification_.WaitForNotification();
  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(BidStatsTest, DeclaresSuccessIfNoBidExplicitlyFailed) {
  BidStats bid_stats(3, context_logger_, [this](bool any_successful) {
    // No bid errored (but there is either a skipped or empty response) =>
    // succeeded overall.
    EXPECT_TRUE(any_successful);
    notification_.Notify();
  });

  std::vector<std::thread> threads;
  for (auto status : {CompletedBidState::ERROR, CompletedBidState::SKIPPED,
                      CompletedBidState::EMPTY_RESPONSE}) {
    threads.emplace_back(
        [&bid_stats, status]() { bid_stats.BidCompleted(status); });
  }

  notification_.WaitForNotification();
  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(BidStatsTest, CallbackCalledOnlyOnce) {
  int count = 0;
  BidStats bid_stats(kNumMaxThreads, context_logger_,
                     [this, &count](bool any_successful) {
                       // No bid errored (but there is either a skipped or empty
                       // response) => succeeded overall.
                       EXPECT_TRUE(any_successful);
                       count++;
                       notification_.Notify();
                     });

  std::vector<std::thread> threads;
  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back(
        [&bid_stats]() { bid_stats.BidCompleted(CompletedBidState::SUCCESS); });
  }

  notification_.WaitForNotification();
  EXPECT_EQ(count, 1);
  for (auto& t : threads) {
    t.join();
  }
}

TEST_F(BidStatsTest, OnSingleBidsDoneCalledWithLock) {
  BidStats bid_stats(kNumMaxThreads, context_logger_,
                     [this](bool any_successful) { notification_.Notify(); });

  std::vector<std::thread> threads;
  int count = 0;
  for (int i = 0; i < kNumMaxThreads; ++i) {
    threads.emplace_back([&bid_stats, &count]() {
      bid_stats.BidCompleted(CompletedBidState::SUCCESS,
                             [&count]() { ++count; });
    });
  }

  notification_.WaitForNotification();
  EXPECT_EQ(count, 10);
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
