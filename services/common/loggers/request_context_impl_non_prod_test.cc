/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "services/common/loggers/request_context_impl_test.h"

namespace privacy_sandbox::bidding_auction_servers::log {
namespace {

using ::testing::ContainsRegex;

TEST_F(ContextLogTest, LogNotConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      ContextImpl::ContextMap{{kToken, "server_tok"}, {"id", "1234"}},
      "mismatched_Token");
  EXPECT_THAT(LogWithCapturedStderr(
                  [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
  EXPECT_EQ(ReadSs(), "");
}

TEST_F(ContextLogTest, LogConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      ContextImpl::ContextMap{{kToken, "server_tok"}, {"id", "1234"}},
      "server_tok");
  EXPECT_THAT(LogWithCapturedStderr(
                  [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
  EXPECT_THAT(ReadSs(),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
