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

#include "benchmark/request_utils.h"

#include <cstdlib>
#include <string>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr int kSeed = 123;

TEST(RequestUtilsTest, GenerateRandomFloat) {
  srand(kSeed);
  std::string expect = std::to_string(static_cast<float>(rand()) /  // NOLINT
                                      static_cast<float>(RAND_MAX));
  srand(kSeed);
  ASSERT_EQ(GenerateRandomFloat(), expect);
}

TEST(RequestUtilsTest, GenerateRandomFloats) {
  const int kNums = 5;
  srand(kSeed);
  std::string expect;
  for (int i = 0; i < kNums; i++) {
    absl::StrAppend(&expect, "\"", GenerateRandomFloat(), "\"",
                    (i == kNums - 1) ? "" : ", ");
  }
  srand(kSeed);
  std::string actual = GenerateRandomFloats(5);
  ASSERT_EQ(actual, expect);
}

TEST(RequestUtilsTest, StringFormat) {
  ASSERT_EQ(StringFormat("{}"), "{}");
  ASSERT_EQ(StringFormat("{GenerateRandomFloats}"), "{GenerateRandomFloats}");
  ASSERT_EQ(StringFormat("{GenerateRandomFloats(0)}"), "{}");

  srand(kSeed);
  std::string expect = absl::StrCat("{", GenerateRandomFloats(3), "}");
  srand(kSeed);
  ASSERT_EQ(StringFormat("{GenerateRandomFloats(3)}"), expect);
  srand(kSeed);
  ASSERT_EQ(StringFormat("{GenerateRandomFloats(2), GenerateRandomFloats(1)}"),
            expect);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
