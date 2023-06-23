// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/util/scoped_cbor.h"

#include "cbor/strings.h"
#include "gtest/gtest.h"

#include "cbor.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

bool HaveSameUnderlyingMemory(cbor_item_t* first, cbor_item_t* second) {
  if ((first && !second) || (second && !first)) {
    return false;
  }
  if (!first && !second) {
    return true;
  }
  return *reinterpret_cast<unsigned char*>(first) ==
         *reinterpret_cast<unsigned char*>(second);
}

TEST(ScopedCborTest, TestCreation) {
  ScopedCbor test_string(cbor_build_string("test"));
  cbor_item_t* expected_underlying_cbor = cbor_build_string("test");

  EXPECT_PRED2(HaveSameUnderlyingMemory, test_string.get(),
               expected_underlying_cbor);
  cbor_decref(&expected_underlying_cbor);
}

// Verifies that the previously allocated string is released when a new
// assignment comes in.
TEST(ScopedCborTest, TestAssignment) {
  ScopedCbor test_string(cbor_build_string("test"));
  test_string = cbor_build_string("test2");

  cbor_item_t* expected_underlying_cbor = cbor_build_string("test2");
  EXPECT_PRED2(HaveSameUnderlyingMemory, test_string.get(),
               expected_underlying_cbor);
  cbor_decref(&expected_underlying_cbor);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
