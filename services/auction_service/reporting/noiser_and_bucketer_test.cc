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

#include <limits>

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
  constexpr double kExpectedAverage = static_cast<double>(kTopOfRange / 2);
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

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
