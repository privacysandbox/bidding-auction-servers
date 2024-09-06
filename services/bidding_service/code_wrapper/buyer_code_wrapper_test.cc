
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
#include "rapidjson/document.h"
#include "services/bidding_service/code_wrapper/buyer_code_wrapper_test_constants.h"
#include "services/bidding_service/constants.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

constexpr char kAdRenderUrlPrefixForCodeWrapperTest[] = "https://ads.com/adId?";

TEST(GetBuyerWrappedCode, GeneratesCompleteFinalJavascript) {
  EXPECT_EQ(
      GetBuyerWrappedCode(absl::StrFormat(kBuyerBaseCode_template,
                                          kAdRenderUrlPrefixForCodeWrapperTest),
                          {}),
      absl::StrFormat(kExpectedGenerateBidCode_template,
                      kAdRenderUrlPrefixForCodeWrapperTest));
}

TEST(GetBuyerWrappedCode,
     GeneratesCompleteFinalJavascriptWithPrivateAggregationWrapper) {
  BuyerCodeWrapperConfig wrapper_config = {.enable_private_aggregate_reporting =
                                               true};
  EXPECT_EQ(
      GetBuyerWrappedCode(absl::StrFormat(kBuyerBaseCode_template,
                                          kAdRenderUrlPrefixForCodeWrapperTest),
                          wrapper_config),
      absl::StrCat(absl::StrFormat(kExpectedGenerateBidCode_template,
                                   kAdRenderUrlPrefixForCodeWrapperTest),
                   kPrivateAggregationWrapperFunction));
}

TEST(GetBuyerWrappedCode, GeneratesCompleteFinalJavascriptWithWasm) {
  BuyerCodeWrapperConfig wrapper_config = {.ad_tech_wasm = "test"};
  std::string expected =
      absl::StrReplaceAll(kExpectedGenerateBidCode_template,
                          {{"const globalWasmHex = [];",
                            "const globalWasmHex = [0x74,0x65,0x73,0x74,];"}});
  EXPECT_EQ(GetBuyerWrappedCode(kBuyerBaseCode_template, wrapper_config),
            expected);
}

TEST(GetBuyerWrappedCode, GeneratesCompleteProtectedAppSignalsFinalJavaScript) {
  BuyerCodeWrapperConfig wrapper_config = {
      .auction_type = AuctionType::kProtectedAppSignals,
      .auction_specific_setup = kEncodedProtectedAppSignalsHandler};
  EXPECT_EQ(GetBuyerWrappedCode(kBuyerBaseCode_template, wrapper_config),
            kExpectedProtectedAppSignalsGenerateBidCodeTemplate);
}

void GenerateFeatureFlagsTestHelper(bool is_logging_enabled,
                                    bool is_debug_url_generation_enabled) {
  std::string actual_json =
      GetFeatureFlagJson(is_logging_enabled, is_debug_url_generation_enabled);
  absl::StatusOr<rapidjson::Document> document = ParseJsonString(actual_json);

  EXPECT_TRUE(document.ok());
  EXPECT_TRUE(document.value().HasMember(kFeatureLogging));
  EXPECT_TRUE(document.value()[kFeatureLogging].IsBool());
  EXPECT_EQ(is_logging_enabled, document.value()[kFeatureLogging].GetBool());
  EXPECT_TRUE(document.value().HasMember(kFeatureDebugUrlGeneration));
  EXPECT_TRUE(document.value()[kFeatureDebugUrlGeneration].IsBool());
  EXPECT_EQ(is_debug_url_generation_enabled,
            document.value()[kFeatureDebugUrlGeneration].GetBool());
}

TEST(GetBuyerWrappedCode, GenerateFeatureFlagsAllConbinations) {
  GenerateFeatureFlagsTestHelper(true, true);
  GenerateFeatureFlagsTestHelper(true, false);
  GenerateFeatureFlagsTestHelper(false, true);
  GenerateFeatureFlagsTestHelper(false, false);
}

TEST(GetBuyerWrappedCode, GeneratesCompleteGenericFinalJavascript) {
  EXPECT_EQ(GetProtectedAppSignalsGenericBuyerWrappedCode(
                absl::StrFormat(kBuyerBaseCode_template,
                                kAdRenderUrlPrefixForCodeWrapperTest),
                /*ad_tech_wasm=*/"", "prepareDataForAdRetrieval", "testArg"),
            absl::StrFormat(kExpectedPrepareDataForAdRetrievalTemplate,
                            kAdRenderUrlPrefixForCodeWrapperTest));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
