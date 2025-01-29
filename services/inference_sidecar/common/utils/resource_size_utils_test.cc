//  Copyright 2024 Google LLC
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
#include "utils/resource_size_utils.h"

#include <cstdint>

#include "googletest/include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {
TEST(ResourceSizeConvertTest, ConvertMbToByteCorrectly) {
  int64_t size_of_one_mb = 1;
  EXPECT_EQ(GetByteSizeFromMb(size_of_one_mb), 1ULL * 1024 * 1024);
}

TEST(ResourceSizeConvertTest, ConvertZeroToZero) {
  int64_t size_of_zero_mb = 0;
  EXPECT_EQ(GetByteSizeFromMb(size_of_zero_mb), 0);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
