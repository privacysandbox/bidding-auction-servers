// Copyright 2024 Google LLC
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
#include "services/auction_service/code_wrapper/seller_udf_wrapper.h"

#include "gtest/gtest.h"
#include "services/auction_service/code_wrapper/seller_udf_wrapper_test_constants.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

TEST(GetSellerWrappedCode, GeneratesWrappedCodeWithScoreAdAndReportResult) {
  bool enable_report_result_url_generation = true;
  std::string observed = GetSellerWrappedCode(
      kSellerBaseCode, enable_report_result_url_generation);
  EXPECT_EQ(observed, kExpectedSellerCodeWithScoreAdAndReportResult)
      << "Observed:\n"
      << observed << "\n\n"
      << "Expected: " << kExpectedSellerCodeWithScoreAdAndReportResult;
}

TEST(GetSellerWrappedCode, LoadsOnlySellerCodeWithReportResultDisabled) {
  bool enable_report_result_url_generation = false;
  EXPECT_EQ(GetSellerWrappedCode(kSellerBaseCode,
                                 enable_report_result_url_generation),
            kExpectedCodeWithReportingDisabled);
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
