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

#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/loggers/request_context_logger.h"
#include "services/common/loggers/request_context_logger_test.h"

namespace privacy_sandbox::bidding_auction_servers::log {

namespace {
using ::testing::ContainsRegex;
using ::testing::HasSubstr;

TEST_F(LogTest, NothingIfNotConsented) {
  EXPECT_EQ(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      "");
  tc.is_debug_response_ = true;
  EXPECT_EQ(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      "");
}

TEST_F(LogTest, OnlyConsentSinkIfConsented) {
  tc.is_consented_ = true;
  EXPECT_CALL(tc.consent_sink_,
              Send(LogEntryHas(absl::StrCat(tc.context_str_, kLogContent))))
      .Times(1);
  EXPECT_EQ(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      "");

  // is_debug_response_ doesn't do anything
  tc.is_debug_response_ = true;
  EXPECT_CALL(tc.consent_sink_,
              Send(LogEntryHas(absl::StrCat(tc.context_str_, kLogContent))))
      .Times(1);
  EXPECT_EQ(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      "");
}

TEST_F(LogTest, NothingIfVerboseHigh) {
  tc.is_consented_ = true;
  // without `EXPECT_CALL(tc.consent_sink_, Send)`
  EXPECT_EQ(LogWithCapturedStderr(
                [this]() { PS_VLOG(kMaxV + 1, tc) << kLogContent; }),
            "");
}

TEST_F(LogTest, SkipStreamingIfNotLog) {
  tc.is_consented_ = true;
  EXPECT_DEATH(PS_VLOG(kMaxV, tc) << Crash(), "");

  // will not hit Crash(), because verbosity high
  PS_VLOG(kMaxV + 1, tc) << Crash();

  // will not hit Crash(), because no logger is logging
  tc.is_consented_ = false;
  PS_VLOG(kMaxV, tc) << Crash();
}

TEST_F(LogTest, Warning) {
  tc.is_consented_ = true;
  tc.is_debug_response_ = true;
  std::string log =
      LogWithCapturedStderr([this]() { PS_LOG(WARNING, tc) << kLogContent; });
  EXPECT_THAT(log, HasSubstr(absl::StrCat(tc.context_str_, kLogContent)));
  EXPECT_THAT(log, ContainsRegex("W[0-9]{4}"));
}

TEST_F(LogTest, Error) {
  tc.is_consented_ = true;
  tc.is_debug_response_ = true;
  std::string log =
      LogWithCapturedStderr([this]() { PS_LOG(ERROR, tc) << kLogContent; });
  EXPECT_THAT(log, HasSubstr(absl::StrCat(tc.context_str_, kLogContent)));
  EXPECT_THAT(log, ContainsRegex("E[0-9]{4}"));
}

TEST_F(LogTest, NoContext) {
  std::string log =
      LogWithCapturedStderr([]() { PS_VLOG(kMaxV) << kLogContent; });
  EXPECT_EQ(log, "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
