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

#include "utils/log.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/log/absl_log.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using ::testing::HasSubstr;

constexpr absl::string_view kLogContent = "log content";

TEST(LogTest, LogIfConsented) {
  PredictResponse predict_response;
  RequestContext request_context(
      [&predict_response]() { return predict_response.mutable_debug_info(); },
      true);

  INFERENCE_LOG(INFO, request_context) << kLogContent;
  ASSERT_EQ(predict_response.debug_info().logs_size(), 1);
  EXPECT_THAT(predict_response.debug_info().logs(0), HasSubstr(kLogContent));
}

TEST(LogTest, LogNothingIfNoConsent) {
  PredictResponse predict_response;
  RequestContext request_context(
      [&predict_response]() { return predict_response.mutable_debug_info(); },
      false);

  INFERENCE_LOG(INFO, request_context) << kLogContent;
  EXPECT_EQ(predict_response.debug_info().logs_size(), 0);
}

TEST(LogTest, CanLogMultipleTimes) {
  PredictResponse predict_response;
  RequestContext request_context(
      [&predict_response]() { return predict_response.mutable_debug_info(); },
      true);

  for (int i = 0; i < 10; ++i) {
    INFERENCE_LOG(INFO, request_context) << kLogContent;
  }
  EXPECT_EQ(predict_response.debug_info().logs_size(), 10);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
