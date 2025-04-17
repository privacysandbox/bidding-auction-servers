//  Copyright 2025 Google LLC
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

#include "services/common/chaffing/moving_median.h"

#include <gmock/gmock-matchers.h>

#include <thread>
#include <vector>

#include <include/gmock/gmock-actions.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

class MovingMedianTest : public testing::Test {
 public:
  MovingMedianTest() : rng_(1) {}

 protected:
  RandomNumberGenerator rng_;
};

TEST_F(MovingMedianTest, WindowNotFull) {
  MovingMedian mm(3, 1);
  mm.AddNumber(rng_, 1);
  mm.AddNumber(rng_, 2);
  auto result = mm.GetMedian();
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(MovingMedianTest, OddWindowSize) {
  MovingMedian mm(3, 1);
  mm.AddNumber(rng_, 1);
  mm.AddNumber(rng_, 2);
  mm.AddNumber(rng_, 3);
  auto result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 2);

  mm.AddNumber(rng_, 4);
  result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 3);
}

TEST_F(MovingMedianTest, EvenWindowSize) {
  MovingMedian mm(4, 1);
  mm.AddNumber(rng_, 1);
  mm.AddNumber(rng_, 2);
  mm.AddNumber(rng_, 3);
  mm.AddNumber(rng_, 4);
  auto result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 2);

  mm.AddNumber(rng_, 5);
  result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 3);
}

TEST_F(MovingMedianTest, DuplicateValues) {
  MovingMedian mm(3, 1);
  mm.AddNumber(rng_, 2);
  mm.AddNumber(rng_, 2);
  mm.AddNumber(rng_, 2);
  auto result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 2);
}

TEST_F(MovingMedianTest, LargeNumbers) {
  MovingMedian mm(3, 1);
  mm.AddNumber(rng_, 2147483647);
  mm.AddNumber(rng_, 2147483646);
  mm.AddNumber(rng_, 2147483645);
  auto result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 2147483646);
}

TEST_F(MovingMedianTest, ConcurrentAddNumberAndGetMedian) {
  MovingMedian mm(10, 1);
  std::vector<std::thread> threads;
  threads.reserve(20);

  for (int i = 0; i < 20; ++i) {
    threads.emplace_back([this, &mm]() {
      for (int j = 0; j < 100; ++j) {
        mm.AddNumber(rng_, j);
        (void)mm.GetMedian();
      }
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(MovingMedianTest, ValidateSamplingProbabilityLogic) {
  // Provide 0 for sampling probability, i.e. no numbers would be added to
  // window once it's filled.
  MovingMedian mm(1, 0);
  mm.AddNumber(rng_, 1);
  ASSERT_TRUE(mm.IsWindowFilled());

  auto result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 1);

  mm.AddNumber(rng_, 999);
  result = mm.GetMedian();
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 1);
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers
