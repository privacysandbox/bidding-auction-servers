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

#include "services/common/util/consented_debugging_logger.h"

#include <gmock/gmock-matchers.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(ConsentedDebuggingLoggerTest, FormatContextWithEmptyString) {
  EXPECT_EQ(FormatContext({}), "");
}

TEST(ConsentedDebuggingLoggerTest, FormatContextWithConsentedDebugConfig) {
  EXPECT_EQ(FormatContext({{"is_consented", "true"}, {"token", "xyz"}}),
            " (is_consented: true, token: xyz) ");
}

TEST(ConsentedDebuggingLoggerTest, NotConsented_NoToken) {
  auto span_context = opentelemetry::trace::SpanContext(/*sampled_flag=*/false,
                                                        /*is_remote=*/false);
  EXPECT_FALSE(ConsentedDebuggingLogger({}, span_context, /*server_token=*/"")
                   .IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({}, span_context, /*server_token=*/"")
                   .IsConsented());
  EXPECT_FALSE(
      ConsentedDebuggingLogger({}, span_context, "test").IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, ""}}, span_context,
                                        /*server_token=*/"")
                   .IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, ""}}, span_context,
                                        /*server_token=*/"")
                   .IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, ""}}, span_context, "test")
                   .IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, "test"}}, span_context,
                                        /*server_token=*/"")
                   .IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, NotConsented_Mismatch) {
  auto span_context = opentelemetry::trace::SpanContext(/*sampled_flag=*/false,
                                                        /*is_remote=*/false);
  EXPECT_FALSE(
      ConsentedDebuggingLogger({{kToken, "test"}}, span_context, "test2")
          .IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, Consented) {
  auto span_context = opentelemetry::trace::SpanContext(/*sampled_flag=*/false,
                                                        /*is_remote=*/false);
  EXPECT_TRUE(ConsentedDebuggingLogger({{kToken, "test"}}, span_context, "test")
                  .IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, SimpleLog) {
  auto span_context = opentelemetry::trace::SpanContext(/*sampled_flag=*/false,
                                                        /*is_remote=*/false);
  auto logger =
      ConsentedDebuggingLogger({{kToken, "test"}}, span_context, "test");
  logger.vlog(0, "hello world");
  EXPECT_TRUE(logger.IsConsented());
}

// TODO(b/279955398): Add a test for the output of vlog().

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
