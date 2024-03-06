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

#include "services/common/util/error_accumulator.h"

#include <gmock/gmock-matchers.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/util/error_categories.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::testing::IsEmpty;

TEST(ErrorAccumulatorTest, EmptyErrorList) {
  ErrorAccumulator error_accumulator;
  EXPECT_THAT(error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE),
              IsEmpty());
  EXPECT_THAT(error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
              IsEmpty());
  EXPECT_FALSE(error_accumulator.HasErrors());
}

TEST(ErrorAccumulatorTest, ReportAndGetError) {
  ErrorAccumulator error_accumulator;
  std::string error_msg = "Bad configuration";
  error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE, error_msg,
                                ErrorCode::CLIENT_SIDE);
  ErrorAccumulator::ErrorMap expected_error_map = {
      {ErrorCode::CLIENT_SIDE, {error_msg}}};
  EXPECT_EQ(error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE),
            expected_error_map);
  EXPECT_THAT(error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
              IsEmpty());
  EXPECT_TRUE(error_accumulator.HasErrors());
}

TEST(ErrorAccumulatorTest, DeduplicatesErrors) {
  ErrorAccumulator error_accumulator;
  std::string error_msg = "Bad configuration";
  error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE, error_msg,
                                ErrorCode::CLIENT_SIDE);
  error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE, error_msg,
                                ErrorCode::CLIENT_SIDE);

  // Same error reported multiple times gets deduplicated.
  ErrorAccumulator::ErrorMap expected_error_map = {
      {ErrorCode::CLIENT_SIDE, {error_msg}}};
  EXPECT_EQ(error_accumulator.GetErrors(ErrorVisibility::AD_SERVER_VISIBLE),
            expected_error_map);
  EXPECT_THAT(error_accumulator.GetErrors(ErrorVisibility::CLIENT_VISIBLE),
              IsEmpty());
  EXPECT_TRUE(error_accumulator.HasErrors());
}

TEST(ErrorAccumulatorTest, ReturnsConcatenatedErrorString) {
  std::string error_1 = "Bad input.";
  std::string error_2 = "Bad config.";
  std::string error_3 = "Bad idea";

  ErrorAccumulator error_accumulator;
  error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error_1,
                                ErrorCode::CLIENT_SIDE);
  error_accumulator.ReportError(ErrorVisibility::CLIENT_VISIBLE, error_2,
                                ErrorCode::CLIENT_SIDE);
  error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE, error_1,
                                ErrorCode::CLIENT_SIDE);
  error_accumulator.ReportError(ErrorVisibility::AD_SERVER_VISIBLE, error_3,
                                ErrorCode::CLIENT_SIDE);

  // Reverse the order of reporting.
  std::string client_visible_error_str = absl::StrCat(error_2, "; ", error_1);
  std::string server_visible_error_str = absl::StrCat(error_3, "; ", error_1);
  EXPECT_EQ(error_accumulator.GetAccumulatedErrorString(
                ErrorVisibility::CLIENT_VISIBLE),
            client_visible_error_str);
  EXPECT_EQ(error_accumulator.GetAccumulatedErrorString(
                ErrorVisibility::AD_SERVER_VISIBLE),
            server_visible_error_str);
}

}  // namespace privacy_sandbox::bidding_auction_servers
