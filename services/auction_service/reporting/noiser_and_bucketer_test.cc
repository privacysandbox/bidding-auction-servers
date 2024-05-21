// Copyright 2023 Google LLC
//
// Licensed under the Apache-form License, Version 2.0 (the "License");
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
#include "services/auction_service/reporting/noiser_and_bucketer.h"

#include <cmath>
#include <limits>

#include "absl/container/flat_hash_set.h"
#include "googletest/include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr uint16_t kMin = 0x0000, kMax = 0x0FFF;

// Verifies that when function(input) is called multiple times:
// -A value equal to the input and a value different from the input is returned.
// -The returned value is in the range of (min, max)
template <typename Function, typename T>
void VerifyRandomNumberGenerated(T input, int expected, Function function,
                                 int min, int max) {
  bool seen_equal = false, seen_not_equal = false;
  while (!seen_equal || !seen_not_equal) {
    absl::StatusOr<int> actual = function(input);
    ASSERT_TRUE(actual.ok());
    EXPECT_GE(actual.value(), min);
    EXPECT_LE(actual.value(), max);
    if (actual.value() == expected) {
      seen_equal = true;
    } else {
      seen_not_equal = true;
    }
  }
}

TEST(NoiseAndMaskModelingSignals, NoisesModelingSignals) {
  for (uint16_t i = 1; i <= kMax; i++) {
    SCOPED_TRACE(i);
    VerifyRandomNumberGenerated(/*input=*/i, /*expected=*/i,
                                /*function=*/NoiseAndMaskModelingSignals,
                                /*min=*/kMin,
                                /*max=*/kMax);
  }
}

TEST(RandGenerator, RandGeneratorIsUniform) {
  // Verify that RandGenerator has a uniform distribution.
  // A degenerate case for such an implementation is e.g. a top of
  // range that is 2/3rds of the way to MAX_UINT64, in which case the
  // bottom half of the range would be twice as likely to occur as the
  // top half. Calculus shows that the largest
  // measurable delta is when the top of the range is 3/4ths of the
  // way, so that's what we use in the test.
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

TEST(NoiserAndBucketerTest, JoinCount) {
  constexpr int kMin = 1, kMax = 16;

  // clang-format off
  const struct {
    int input;
    int expected;
  } kTestCases[] = {
      {-2, 1},
      {-1, 1},
      {0, 1},
      {1, 1},
      {2, 2},
      {3, 3},
      {4, 4},
      {5, 5},
      {6, 6},
      {7, 7},
      {8, 8},
      {9, 9},
      {10, 10},
      {11, 11},
      {19, 11},
      {20, 11},
      {21, 12},
      {29, 12},
      {30, 12},
      {31, 13},
      {32, 13},
      {39, 13},
      {40, 13},
      {41, 14},
      {42, 14},
      {49, 14},
      {50, 14},
      {51, 15},
      {52, 15},
      {60, 15},
      {70, 15},
      {80, 15},
      {90, 15},
      {99, 15},
      {100, 15},
      {101, 16},
      {200, 16},
      {1000, 16},
  };
  // clang-format on
  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.input);
    EXPECT_EQ(BucketJoinCount(test_case.input), test_case.expected);
    VerifyRandomNumberGenerated(/*input=*/test_case.input,
                                /*expected=*/test_case.expected,
                                /*function=*/NoiseAndBucketJoinCount,
                                /*min=*/kMin, /*max=*/kMax);
  }
}

TEST(NoiserAndBucketerTest, Recency) {
  constexpr int kMin = 0, kMax = 31;

  // clang-format off
  const struct {
    int input;
    int expected;
  } kTestCases[] = {
    {-2, 0},
    {-1, 0},
    {0, 0},
    {1, 1},
    {2, 2},
    {3, 3},
    {4, 4},
    {5, 5},
    {6, 6},
    {7, 7},
    {8, 8},
    {9, 9},
    {10, 10},
    {11, 10},
    {13, 10},
    {14, 10},
    {15, 11},
    {16, 11},
    {18, 11},
    {19, 11},
    {20, 12},
    {21, 12},
    {28, 12},
    {29, 12},
    {30, 13},
    {31, 13},
    {38, 13},
    {39, 13},
    {40, 14},
    {41, 14},
    {48, 14},
    {49, 14},
    {50, 15},
    {51, 15},
    {58, 15},
    {59, 15},
    {60, 16},
    {61, 16},
    {73, 16},
    {74, 16},
    {75, 17},
    {76, 17},
    {88, 17},
    {89, 17},
    {90, 18},
    {91, 18},
    {103, 18},
    {104, 18},
    {105, 19},
    {106, 19},
    {118, 19},
    {119, 19},
    {120, 20},
    {121, 20},
    {238, 20},
    {239, 20},
    {240, 21},
    {241, 21},
    {718, 21},
    {719, 21},
    {720, 22},
    {721, 22},
    {1438, 22},
    {1439, 22},
    {1440, 23},
    {1441, 23},
    {2158, 23},
    {2159, 23},
    {2160, 24},
    {2161, 24},
    {2878, 24},
    {2879, 24},
    {2880, 25},
    {2881, 25},
    {4318, 25},
    {4319, 25},
    {4320, 26},
    {4321, 26},
    {5758, 26},
    {5759, 26},
    {5760, 27},
    {5761, 27},
    {10078, 27},
    {10079, 27},
    {10080, 28},
    {10081, 28},
    {20158, 28},
    {20159, 28},
    {20160, 29},
    {20161, 29},
    {30238, 29},
    {30239, 29},
    {30240, 30},
    {30241, 30},
    {40318, 30},
    {40319, 30},
    {40320, 31},
    {40321, 31},
    {1000000, 31},
    {1000000000, 31},
  };
  // clang-format on
  for (const auto& test_case : kTestCases) {
    SCOPED_TRACE(test_case.input);
    EXPECT_EQ(BucketRecency(test_case.input), test_case.expected);
    VerifyRandomNumberGenerated(/*input=*/test_case.input,
                                /*expected=*/test_case.expected,
                                /*function=*/NoiseAndBucketRecency,
                                /*min=*/kMin, /*max=*/kMax);
  }
}

TEST(RoundStochasticallyToKBits, MatchesTable) {
  struct {
    double input;
    unsigned k;
    double expected_output;
  } test_cases[] = {
      {0, 8, 0},
      {1, 8, 1},
      {-1, 8, -1},
      // infinity passes through
      {std::numeric_limits<double>::infinity(), 7,
       std::numeric_limits<double>::infinity()},
      {-std::numeric_limits<double>::infinity(), 7,
       -std::numeric_limits<double>::infinity()},
      // not clipped
      {255, 8, 255},
      // positive overflow
      {2e38, 8, std::numeric_limits<double>::infinity()},
      // positive underflow
      {1e-39, 8, 0},
      // negative overflow
      {-2e38, 8, -std::numeric_limits<double>::infinity()},
      // negative underflow
      {-1e-39, 8, -0},
  };

  for (const auto& test_case : test_cases) {
    EXPECT_EQ(test_case.expected_output,
              RoundStochasticallyToKBits(test_case.input, test_case.k).value())
        << "with " << test_case.input << " and " << test_case.k;
  }
}

TEST(RoundStochasticallyToKBits, PassesNaN) {
  EXPECT_TRUE(std::isnan(
      RoundStochasticallyToKBits(std::numeric_limits<double>::quiet_NaN(), 8)
          .value()));
  EXPECT_TRUE(std::isnan(RoundStochasticallyToKBits(
                             std::numeric_limits<double>::signaling_NaN(), 8)
                             .value()));
}

TEST(RoundStochasticallyToKBits, IsNonDeterministic) {
  // Since 0.3 can't be represented with 8 bits of precision, this value will be
  // clipped to either the nearest lower number or nearest higher number.
  const double kInput = 0.3;
  absl::flat_hash_set<double> seen;
  const absl::flat_hash_set<double> expected_values = {0.298828125, 0.30078125};
  while (seen.size() < 2) {
    double result = RoundStochasticallyToKBits(kInput, 8).value();
    ASSERT_TRUE(
        std::any_of(expected_values.begin(), expected_values.end(),
                    [&result](float number) { return result == number; }));
    seen.insert(result);
  }
}

TEST(RoundStochasticallyToKBits, RoundsUpAndDown) {
  const double inputs[] = {129.3, 129.8};
  const absl::flat_hash_set<double> expected_values = {129.0, 130.0};
  for (auto input : inputs) {
    SCOPED_TRACE(input);
    absl::flat_hash_set<double> seen;
    while (seen.size() < 2) {
      double result = RoundStochasticallyToKBits(input, 8).value();
      ASSERT_TRUE(
          std::any_of(expected_values.begin(), expected_values.end(),
                      [&result](float number) { return result == number; }));
      seen.insert(result);
    }
  }
}

TEST(RoundStochasticallyToKBits, HandlesOverflow) {
  double max_value =
      std::ldexp(0.998046875, std::numeric_limits<int8_t>::max());
  double expected_value =
      std::ldexp(0.99609375, std::numeric_limits<int8_t>::max());
  const double inputs[] = {
      max_value,
      -max_value,
  };
  for (auto input : inputs) {
    SCOPED_TRACE(input);
    absl::flat_hash_set<double> expected_numbers = {
        std::copysign(expected_value, input),
        std::copysign(std::numeric_limits<double>::infinity(), input)};
    absl::flat_hash_set<double> seen;
    while (seen.size() < 2) {
      double result = RoundStochasticallyToKBits(input, 8).value();
      ASSERT_TRUE(
          std::any_of(expected_numbers.begin(), expected_numbers.end(),
                      [&result](float number) { return result == number; }));
      seen.insert(result);
    }
  }
}

// Test that random rounding allows mean to approximate the true value.
TEST(RoundStochasticallyToKBits, ApproximatesTrueSum) {
  // Since 0.3 can't be represented with 8 bits of precision, this value will be
  // clipped randomly. Because 0.3 is 60% of the way from the nearest
  // representable number smaller than it and 40% of the way to the nearest
  // representable number larger than it, the value should be rounded down to
  // 0.2988... 60% of the time and rounded up to 0.30078... 40% of the time.
  // This ensures that if you add the result N times you roughly get 0.3 * N.
  const size_t kIterations = 10000;
  const double kInput = 0.3;
  double total = 0;

  for (size_t idx = 0; idx < kIterations; idx++) {
    total += RoundStochasticallyToKBits(kInput, 8).value();
  }

  EXPECT_GT(total, 0.9 * kInput * kIterations);
  EXPECT_LT(total, 1.1 * kInput * kIterations);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
