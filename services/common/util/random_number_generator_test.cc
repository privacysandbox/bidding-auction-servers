// Copyright 2025 Google LLC
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

#include "services/common/util/random_number_generator.h"

#include <limits>

#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(RandGenerator, FailsForRangeZero) {
  absl::StatusOr<uint64_t> value = RandGenerator(0);
  ASSERT_FALSE(value.status().ok());
}

TEST(RandGenerator, ReturnsZeroForRangeOne) {
  absl::StatusOr<uint64_t> value = RandGenerator(1);
  ASSERT_TRUE(value.status().ok());
  EXPECT_EQ(*value, 0);
}

TEST(RandGenerator, ReturnsOutputWithUniformDistribution) {
  // A degenerate case for such an implementation is e.g. a top of range that is
  // 2/3rds of the way to MAX_UINT64, in which case the bottom half of the range
  // would be twice as likely to occur as the top half. Calculus shows that the
  // largest measurable delta is when the top of the range is 3/4ths of the way,
  // so that's what we use in the test.
  constexpr uint64_t kTopOfRange =
      (std::numeric_limits<uint64_t>::max() / 4ULL) * 3ULL;
  constexpr double kExpectedAverage = static_cast<double>(kTopOfRange) / 2.0;
  constexpr double kAllowedVariance = kExpectedAverage / 50.0;  // +/- 2%
  constexpr int kMinAttempts = 1000;
  constexpr int kMaxAttempts = 1000000;

  double cumulative_average = 0.0;
  int count = 0;
  while (count < kMaxAttempts) {
    uint64_t value = RandGenerator(kTopOfRange).value();
    cumulative_average = (count * cumulative_average + value) / (count + 1);

    // Don't quit too quickly for things to start converging, or we may have
    // a false positive.
    if (count > kMinAttempts &&
        kExpectedAverage - kAllowedVariance < cumulative_average &&
        cumulative_average < kExpectedAverage + kAllowedVariance) {
      break;
    }
    ++count;
  }

  ASSERT_LT(count, kMaxAttempts) << "Expected average was " << kExpectedAverage
                                 << ", average ended at " << cumulative_average;
}

TEST(RandInt, WorksForMinEqualsMax) {
  absl::StatusOr<int> value = RandInt(1, 1);
  ASSERT_TRUE(value.status().ok());
  EXPECT_EQ(*value, 1);
}

TEST(RandInt, WorksForFullRange) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();
  absl::StatusOr<int> value = RandInt(kMin, kMax);
  EXPECT_TRUE(value.status().ok());
  EXPECT_GE(*value, kMin);
  EXPECT_LE(*value, kMax);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
