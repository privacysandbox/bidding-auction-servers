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

#include "services/common/util/multi_logger.h"

#include <gmock/gmock-matchers.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/util/consented_debugging_logger.h"
#include "services/common/util/context_logger.h"
#include "services/common/util/request_response_constants.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr char kTestToken[] = "test";
constexpr char kMismatchedToken[] = "test2";
constexpr int kVerboseLevel = 3;

int GetVLogLevel() { return FLAGS_v; }

// Note that this function is not thread safe.
// Please do not run the tests in parallel.
void SetVLogLevel(int level) { FLAGS_v = level; }

// TODO: Use TEST_F instead.
TEST(MultiLoggerTest, ShouldNotLog) {
  // No log from ContextLogger.
  int level = GetVLogLevel();
  SetVLogLevel(0);
  MultiLogger multi_logger({{kToken, kTestToken}}, kMismatchedToken);
  EXPECT_FALSE(multi_logger.ShouldLog(kVerboseLevel));
  SetVLogLevel(level);
}

TEST(MultiLoggerTest, ShouldLogForConsentedLogger) {
  // No log from ContextLogger.
  int level = GetVLogLevel();
  SetVLogLevel(0);
  MultiLogger multi_logger({{kToken, kTestToken}}, kTestToken);
  EXPECT_TRUE(multi_logger.ShouldLog(kVerboseLevel));
  SetVLogLevel(level);
}

TEST(MultiLoggerTest, ShouldLogForContextLogger) {
  int level = GetVLogLevel();
  SetVLogLevel(kVerboseLevel);
  MultiLogger multi_logger({{kToken, kTestToken}}, kMismatchedToken);
  EXPECT_TRUE(multi_logger.ShouldLog(kVerboseLevel));
  SetVLogLevel(level);
}

TEST(MultiLoggerTest, SimpleLog) {
  MultiLogger multi_logger({{kToken, kTestToken}}, kTestToken);
  multi_logger.vlog(0, "hello world");
}

// TODO(b/299366050): Add mockers for ContextLogger and
// ConsentedDebuggingLogger.
// TODO(b/299366050): Add tests for vlog() with depedency injection.

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
