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
      absl::btree_map<std::string, std::string>{{"id", "1234"}}, kServerToken,
      mismatched_token_);
  EXPECT_EQ(LogWithCapturedStderr(
                [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
            "");
  EXPECT_EQ(ReadSs(), "");
}

TEST_F(ConsentedLogTest, LogConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}}, kServerToken,
      matched_token_);
  EXPECT_EQ(LogWithCapturedStderr(
                [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
            "");
  EXPECT_THAT(ReadSs(),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
}

TEST_F(DebugResponseTest, NotLoggedInProd) {
  // mismatched_token_ doesn't log
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}}, kServerToken,
      mismatched_token_,
      [this]() { return ad_response_.mutable_debug_info(); });
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_FALSE(ad_response_.has_debug_info());

  // matched_token_ doesn't log
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}}, kServerToken,
      matched_token_, [this]() { return ad_response_.mutable_debug_info(); });
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_THAT(ReadSs(),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
  EXPECT_FALSE(ad_response_.has_debug_info());

  // debug_info turned on, but doesn't log
  test_instance_ = std::make_unique<ContextImpl>(
      absl::btree_map<std::string, std::string>{{"id", "1234"}}, kServerToken,
      debug_info_config_,
      [this]() { return ad_response_.mutable_debug_info(); });
  PS_VLOG(kMaxV, *test_instance_) << kLogContent;
  EXPECT_FALSE(ad_response_.has_debug_info());
}

TEST(FormatContext, NoContextGeneratesEmptyString) {
  EXPECT_EQ(FormatContext({}), "");
}

TEST(FormatContext, SingleKeyValFormatting) {
  EXPECT_EQ(FormatContext({{"key1", "val1"}}), " (key1: val1) ");
}

TEST(FormatContext, MultipleKeysLexicographicallyOrdered) {
  EXPECT_EQ(FormatContext({{"key1", "val1"}, {"key2", "val2"}}),
            " (key1: val1, key2: val2) ");
}

TEST(FormatContext, OptionalValuesNotInTheFormattedOutput) {
  EXPECT_EQ(FormatContext({{"key1", ""}}), "");
}

TEST_F(ConsentedLogTest, NotConsented) {
  // default
  EXPECT_FALSE(
      ContextImpl({}, /*server_token=*/"", ConsentedDebugConfiguration())
          .is_consented());
  // no client token
  EXPECT_FALSE(ContextImpl({}, kServerToken, ConsentedDebugConfiguration())
                   .is_consented());
  // empty server and client token
  auto empty_client_token = ParseTextOrDie<ConsentedDebugConfiguration>(R"pb(
    is_consented: true
    token: ""
  )pb");
  EXPECT_FALSE(ContextImpl({},
                           /*server_token=*/"", empty_client_token)
                   .is_consented());
  // empty client token, valid server token
  EXPECT_FALSE(
      ContextImpl({}, kServerToken, empty_client_token).is_consented());

  // valid client token, empty server token
  EXPECT_FALSE(ContextImpl({},
                           /*server_token=*/"", matched_token_)
                   .is_consented());
  // mismatch
  EXPECT_FALSE(ContextImpl({}, kServerToken, mismatched_token_).is_consented());
}

TEST_F(ConsentedLogTest, ConsentRevocation) {
  EXPECT_TRUE(ContextImpl({}, kServerToken, matched_token_).is_consented());

  matched_token_.set_is_consented(false);
  EXPECT_FALSE(ContextImpl({}, kServerToken, matched_token_).is_consented());
}

TEST_F(ConsentedLogTest, Update) {
  auto logger = ContextImpl({}, kServerToken, matched_token_);
  EXPECT_TRUE(logger.is_consented());
  logger.Update({}, ConsentedDebugConfiguration());
  EXPECT_FALSE(logger.is_consented());
  logger.Update({}, matched_token_);
  EXPECT_TRUE(logger.is_consented());
  logger.Update({}, mismatched_token_);
  EXPECT_FALSE(logger.is_consented());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
