//   Copyright 2024 Google LLC
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//

#include "services/common/util/data_util.h"

#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(DataUtil, FindIndexInArray) {
  std::array<absl::string_view, 2> test_array = {"item1", "item2"};
  EXPECT_EQ(FindItemIndex(test_array, "item2"), 1);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
