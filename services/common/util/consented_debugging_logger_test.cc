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

constexpr char kTestToken[] = "test";
constexpr char kMismatchedToken[] = "test2";

TEST(ConsentedDebuggingLoggerTest, FormatContextWithEmptyString) {
  EXPECT_EQ(FormatContext({}), "");
}

TEST(ConsentedDebuggingLoggerTest, FormatContext) {
  EXPECT_EQ(FormatContext({{"generation_id", "test"}}),
            " (generation_id: test) ");
}

TEST(ConsentedDebuggingLoggerTest, FormatContextWithConsentedDebugConfig) {
  EXPECT_EQ(FormatContext({{kToken, "test"}}), "");
}

TEST(ConsentedDebuggingLoggerTest, NotConsented_NoToken) {
  EXPECT_FALSE(ConsentedDebuggingLogger({}, /*server_token=*/"").IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({}, /*server_token=*/"").IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({}, kTestToken).IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, ""}},
                                        /*server_token=*/"")
                   .IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, ""}},
                                        /*server_token=*/"")
                   .IsConsented());
  EXPECT_FALSE(
      ConsentedDebuggingLogger({{kToken, ""}}, kTestToken).IsConsented());
  EXPECT_FALSE(ConsentedDebuggingLogger({{kToken, kTestToken}},
                                        /*server_token=*/"")
                   .IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, NotConsented_Mismatch) {
  EXPECT_FALSE(
      ConsentedDebuggingLogger({{kToken, kTestToken}}, kMismatchedToken)
          .IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, Consented) {
  EXPECT_TRUE(ConsentedDebuggingLogger({{kToken, kTestToken}}, kTestToken)
                  .IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, SetContext) {
  auto logger = ConsentedDebuggingLogger({{kToken, kTestToken}}, kTestToken);
  EXPECT_TRUE(logger.IsConsented());
  logger.SetContext({{}});
  EXPECT_FALSE(logger.IsConsented());
  logger.SetContext({{kToken, kTestToken}});
  EXPECT_TRUE(logger.IsConsented());
  logger.SetContext({{kToken, kMismatchedToken}});
  EXPECT_FALSE(logger.IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, SimpleLog) {
  auto logger = ConsentedDebuggingLogger({{kToken, kTestToken}}, kTestToken);
  logger.vlog(0, "hello world");
  EXPECT_TRUE(logger.IsConsented());
}

TEST(ConsentedDebuggingLoggerTest, DoNotAddConsentedDebugConfigToContext) {
  ContextLogger::ContextMap map = {};
  ConsentedDebugConfiguration config;
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, map));

  config.set_is_consented(true);
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, map));

  config.set_is_consented(false);
  config.set_token(kTestToken);
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, map));
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, map));
}

TEST(ConsentedDebuggingLoggerTest, AddConsentedDebugConfigToContext) {
  ContextLogger::ContextMap map = {};
  ConsentedDebugConfiguration config;
  config.set_is_consented(true);
  config.set_token(kTestToken);
  ContextLogger::ContextMap expected_map = {{kToken, kTestToken}};
  EXPECT_TRUE(MaybeAddConsentedDebugConfig(config, map));
  EXPECT_EQ(map, expected_map);
}

// TODO(b/279955398): Add a test for the output of vlog().

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
