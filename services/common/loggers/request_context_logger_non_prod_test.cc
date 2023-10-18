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

using ::testing::HasSubstr;

TEST_F(LogTest, Stderr) {
  EXPECT_THAT(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      HasSubstr(absl::StrCat(tc.context_str_, kLogContent)));
}

TEST_F(LogTest, StderrAndConsent) {
  tc.is_consented_ = true;
  EXPECT_CALL(tc.consent_sink_,
              Send(LogEntryHas(absl::StrCat(tc.context_str_, kLogContent))))
      .Times(1);
  EXPECT_THAT(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      HasSubstr(absl::StrCat(tc.context_str_, kLogContent)));
}

TEST_F(LogTest, StderrAndDebugResponse) {
  tc.is_debug_response_ = true;
  EXPECT_CALL(tc.debug_response_sink_,
              Send(LogEntryHas(absl::StrCat(tc.context_str_, kLogContent))))
      .Times(1);
  EXPECT_THAT(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      HasSubstr(absl::StrCat(tc.context_str_, kLogContent)));
}

TEST_F(LogTest, StderrAndConsentAndDebugResponse) {
  tc.is_consented_ = true;
  EXPECT_CALL(tc.consent_sink_,
              Send(LogEntryHas(absl::StrCat(tc.context_str_, kLogContent))))
      .Times(1);
  tc.is_debug_response_ = true;
  EXPECT_CALL(tc.debug_response_sink_,
              Send(LogEntryHas(absl::StrCat(tc.context_str_, kLogContent))))
      .Times(1);
  EXPECT_THAT(
      LogWithCapturedStderr([this]() { PS_VLOG(kMaxV, tc) << kLogContent; }),
      HasSubstr(absl::StrCat(tc.context_str_, kLogContent)));
}

TEST_F(LogTest, NothingIfVerboseHigh) {
  tc.is_consented_ = true;
  tc.is_debug_response_ = true;
  // without `EXPECT_CALL(tc.consent_sink_, Send)`
  // without `EXPECT_CALL(tc.debug_response_sink_, Send)`
  EXPECT_EQ(LogWithCapturedStderr(
                [this]() { PS_VLOG(kMaxV + 1, tc) << kLogContent; }),
            "");
}

TEST_F(LogTest, SkipStreamingIfNotLog) {
  EXPECT_DEATH(PS_VLOG(kMaxV, tc) << Crash(), "");

  // will not hit Crash(), because verbosity high
  PS_VLOG(kMaxV + 1, tc) << Crash();
}

TEST_F(LogTest, NoContext) {
  std::string log =
      LogWithCapturedStderr([]() { PS_VLOG(kMaxV) << kLogContent; });
  EXPECT_THAT(log, HasSubstr(kLogContent));

  log = LogWithCapturedStderr([]() { PS_VLOG(kMaxV + 1) << kLogContent; });
  EXPECT_EQ(log, "");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
