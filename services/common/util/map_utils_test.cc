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

#include "services/common/util/map_utils.h"

#include <gmock/gmock.h>

#include <string>

#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(GenericKeySetTest, EmptyMap) {
  absl::flat_hash_map<int, std::string> empty_input;
  EXPECT_TRUE(KeySet(empty_input).empty());
}

TEST(GenericKeySetTest, NonEmptyMap) {
  absl::flat_hash_map<int, double> input = {{1, 1.1}, {2, 2.2}, {3, 3.3}};
  absl::flat_hash_set<int> expected_keys = {1, 2, 3};
  EXPECT_EQ(KeySet(input), expected_keys);
}

TEST(StringKeySetTest, EmptyMap) {
  absl::flat_hash_map<std::string, int> empty_input;
  EXPECT_TRUE(KeySet(empty_input).empty());
}

TEST(StringKeySetTest, MapWithIntValues) {
  absl::flat_hash_map<std::string, int> input_map = {
      {"apple", 1},
      {"BaNaNa", 2},
      {"cherry", 3},
  };
  absl::flat_hash_set<absl::string_view> expected_keys = {
      "apple",
      "BaNaNa",
      "cherry",
  };

  EXPECT_EQ(KeySet(input_map), expected_keys);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
