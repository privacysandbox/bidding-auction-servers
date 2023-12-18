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

TEST_F(ConsentedLogTest, LogNotConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}},
      mismatched_token_);
  SetServerTokenForTestOnly(kServerToken);
  EXPECT_THAT(LogWithCapturedStderr(
                  [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
  EXPECT_EQ(ReadSs(), "");
}

TEST_F(ConsentedLogTest, LogConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}},
      matched_token_);
  SetServerTokenForTestOnly(kServerToken);
  EXPECT_THAT(LogWithCapturedStderr(
                  [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
  EXPECT_THAT(ReadSs(),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
}

TEST_F(DebugResponseTest, NotLoggedIfNotSet) {
  // mismatched_token_ does not log debug info
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}},
      mismatched_token_,
      [this]() { return ad_response_.mutable_debug_info(); });
  SetServerTokenForTestOnly(kServerToken);
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_FALSE(ad_response_.has_debug_info());

  // matched_token_ does not log debug info
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}}, matched_token_,
      [this]() { return ad_response_.mutable_debug_info(); });
  SetServerTokenForTestOnly(kServerToken);
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_FALSE(ad_response_.has_debug_info());
}

TEST_F(DebugResponseTest, LoggedIfSet) {
  // debug_info turned on, then log
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}},
      debug_info_config_,
      [this]() { return ad_response_.mutable_debug_info(); });
  SetServerTokenForTestOnly(kServerToken);
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_THAT(ad_response_.debug_info().logs(),
              ElementsAre(ContainsRegex(
                  absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent))));

  // turn off debug info
  ad_response_.clear_debug_info();
  debug_info_config_.set_is_debug_info_in_response(false);
  test_instance_->Update(
      absl::btree_map<std::string, std::string>{{"id", "1234"}},
      debug_info_config_);
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_FALSE(ad_response_.has_debug_info());

  // turn on debug info
  ad_response_.clear_debug_info();
  debug_info_config_.set_is_debug_info_in_response(true);
  test_instance_->Update(
      absl::btree_map<std::string, std::string>{{"id", "1234"}},
      debug_info_config_);
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_THAT(ad_response_.debug_info().logs(),
              ElementsAre(ContainsRegex(
                  absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent))));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
