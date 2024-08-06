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
#include "services/auction_service/code_wrapper/seller_code_wrapper.h"

#include <future>
#include <vector>

#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "services/auction_service/code_wrapper/seller_code_wrapper_test_constants.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper_test_constants.h"
#include "services/common/util/json_util.h"
#include "services/common/util/reporting_util.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {
TEST(GetSellerWrappedCode, GeneratesCompleteFinalCode) {
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  absl::flat_hash_map<std::string, std::string> buyer_code_map;
  buyer_code_map.try_emplace(kBuyerOrigin, kBuyerBaseCode);
  std::string observed =
      GetSellerWrappedCode(kSellerBaseCode, enable_report_result_url_generation,
                           enable_report_win_url_generation, buyer_code_map);
  EXPECT_EQ(observed, kExpectedFinalCode) << "Observed:\n"
                                          << observed << "\n\n"
                                          << "Expected: " << kExpectedFinalCode;
}

TEST(GetSellerWrappedCode, GeneratesCompleteFinalCodeWithProtectedAppSignals) {
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = true;
  absl::flat_hash_map<std::string, std::string> buyer_code_map;
  buyer_code_map.try_emplace(kBuyerOrigin, kBuyerBaseCodeSimple);

  absl::flat_hash_map<std::string, std::string>
      protected_app_signals_buyer_code_map;
  protected_app_signals_buyer_code_map.try_emplace(
      kBuyerOrigin, kProtectedAppSignalsBuyerBaseCode);

  std::string observed = GetSellerWrappedCode(
      kSellerBaseCode, enable_report_result_url_generation,
      /*enable_protected_app_signals=*/true, enable_report_win_url_generation,
      buyer_code_map, protected_app_signals_buyer_code_map);
  EXPECT_EQ(observed, kExpectedProtectedAppSignalsFinalCode)
      << "Observed:\n"
      << observed << "\n\n"
      << "Expected: " << kExpectedProtectedAppSignalsFinalCode;
}

TEST(GetSellerWrappedCode, CodeWithReportWinDisabled) {
  bool enable_report_result_url_generation = true;
  bool enable_report_win_url_generation = false;
  EXPECT_EQ(
      GetSellerWrappedCode(kSellerBaseCode, enable_report_result_url_generation,
                           enable_report_win_url_generation, {}),
      kExpectedCodeWithReportWinDisabled);
}

TEST(GetSellerWrappedCode, CodeWithReportResultDisabled) {
  bool enable_report_result_url_generation = false;
  bool enable_report_win_url_generation = false;
  EXPECT_EQ(
      GetSellerWrappedCode(kSellerBaseCode, enable_report_result_url_generation,
                           enable_report_win_url_generation, {}),
      kExpectedCodeWithReportingDisabled);
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

TEST(GetSellerWrappedCode, GenerateFeatureFlagsAllConbinations) {
  GenerateFeatureFlagsTestHelper(true, true);
  GenerateFeatureFlagsTestHelper(true, false);
  GenerateFeatureFlagsTestHelper(false, true);
  GenerateFeatureFlagsTestHelper(false, false);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
