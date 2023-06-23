// Copyright 2023 Google LLC
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

#include "services/common/util/context_logger.h"

#include <gmock/gmock-matchers.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(ContextLoggerTest, NoContextGeneratesEmptyString) {
  EXPECT_EQ(FormatContext({}), "");
}

TEST(ContextLoggerTest, SingleKeyValFormatting) {
  EXPECT_EQ(FormatContext({{"key1", "val1"}}), " (key1: val1) ");
}

TEST(ContextLoggerTest, MultipleKeysLexicographicallyOrdered) {
  EXPECT_EQ(FormatContext({{"key1", "val1"}, {"key2", "val2"}}),
            " (key1: val1, key2: val2) ");
}

TEST(ContextLoggerTest, OptionalValuesNotInTheFormattedOutput) {
  EXPECT_EQ(FormatContext({{"key1", ""}}), "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
