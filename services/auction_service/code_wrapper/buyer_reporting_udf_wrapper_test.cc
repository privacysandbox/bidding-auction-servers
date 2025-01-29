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
#include "services/auction_service/code_wrapper/buyer_reporting_udf_wrapper.h"

#include <include/gmock/gmock-matchers.h>

#include "gtest/gtest.h"
#include "services/auction_service/code_wrapper/buyer_reporting_test_constants.h"
#include "services/auction_service/code_wrapper/generated_private_aggregation_wrapper.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::StrEq;

TEST(GetBuyerWrappedCode, GeneratesCompleteFinalCodeForPA) {
  std::string observed = GetBuyerWrappedCode(kTestReportWinUdfWithValidation);
  EXPECT_THAT(observed, StrEq(kExpectedBuyerCodeWithReportWinForPA));
}

TEST(GetBuyerWrappedCode, GeneratesCompleteFinalCodeForPAS) {
  bool enable_protected_app_signals = true;
  std::string observed = GetBuyerWrappedCode(kTestReportWinUdfWithValidation,
                                             enable_protected_app_signals);
  EXPECT_THAT(observed, StrEq(kExpectedBuyerCodeWithReportWinForPAS));
}

TEST(GetBuyerWrappedCode, GeneratesWrappedCodeWithPrivateAggregationWrapper) {
  bool enable_protected_app_signals = false;
  bool enable_private_aggregate_reporting = true;
  std::string observed = GetBuyerWrappedCode(
      kTestReportWinUdfWithValidation, enable_protected_app_signals,
      enable_private_aggregate_reporting);
  EXPECT_THAT(observed,
              testing::StrEq(absl::StrCat(kExpectedBuyerCodeWithReportWinForPA,
                                          kPrivateAggregationWrapperFunction)));
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
