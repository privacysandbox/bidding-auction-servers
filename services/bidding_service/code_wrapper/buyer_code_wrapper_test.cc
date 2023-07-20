
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
#include "services/bidding_service/code_wrapper/buyer_code_wrapper.h"

#include <future>
#include <vector>

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper_test_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr char kAdRenderUrlPrefixForCodeWrapperTest[] = "https://ads.com/adId?";

TEST(GetBuyerWrappedCode, GeneratesCompleteFinalJavascript) {
  EXPECT_EQ(
      GetBuyerWrappedCode(absl::StrFormat(kBuyerBaseCode_template,
                                          kAdRenderUrlPrefixForCodeWrapperTest),
                          ""),
      absl::StrFormat(kExpectedGenerateBidCode_template,
                      kAdRenderUrlPrefixForCodeWrapperTest));
}

TEST(GetBuyerWrappedCode, GeneratesCompleteFinalJavascriptWithWasm) {
  std::string expected =
      absl::StrReplaceAll(kExpectedGenerateBidCode_template,
                          {{"const globalWasmHex = [];",
                            "const globalWasmHex = [0x74,0x65,0x73,0x74,];"}});
  EXPECT_EQ(GetBuyerWrappedCode(kBuyerBaseCode_template, "test"), expected);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
