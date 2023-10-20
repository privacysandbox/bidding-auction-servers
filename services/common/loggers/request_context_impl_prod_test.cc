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

TEST(RequestContextImpl, Consented) {
  ContextImpl mismatch_context({{kToken, "server_tok"}}, "mismatched_Token");
  EXPECT_FALSE(mismatch_context.is_consented());

  ContextImpl mismatch_context_matched({{kToken, "server_tok"}}, "server_tok");
  EXPECT_TRUE(mismatch_context_matched.is_consented());
}

TEST_F(ContextLogTest, LogNotConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      ContextImpl::ContextMap{{kToken, "server_tok"}, {"id", "1234"}},
      "mismatched_Token");
  EXPECT_EQ(LogWithCapturedStderr(
                [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
            "");
  EXPECT_EQ(ReadSs(), "");
}

TEST_F(ContextLogTest, LogConsented) {
  test_instance_ = std::make_unique<ContextImpl>(
      ContextImpl::ContextMap{{kToken, "server_tok"}, {"id", "1234"}},
      "server_tok");
  EXPECT_EQ(LogWithCapturedStderr(
                [this]() { PS_VLOG(kMaxV, *test_instance_) << kLogContent; }),
            "");
  EXPECT_THAT(ReadSs(),
              ContainsRegex(absl::StrCat("\\(id: 1234\\)[ \t]+", kLogContent)));
}

TEST(FormatContext, NoContextGeneratesEmptyString) {
  EXPECT_EQ(ContextImpl::FormatContext({}), "");
}

TEST(FormatContext, SingleKeyValFormatting) {
  EXPECT_EQ(ContextImpl::FormatContext({{"key1", "val1"}}), " (key1: val1) ");
}

TEST(FormatContext, MultipleKeysLexicographicallyOrdered) {
  EXPECT_EQ(ContextImpl::FormatContext({{"key1", "val1"}, {"key2", "val2"}}),
            " (key1: val1, key2: val2) ");
}

TEST(FormatContext, OptionalValuesNotInTheFormattedOutput) {
  EXPECT_EQ(ContextImpl::FormatContext({{"key1", ""}}), "");
}

constexpr char kTestToken[] = "test";

TEST(MaybeAddConsentedDebugConfig, DoNotAddConsentedDebugConfigToContext) {
  ContextImpl::ContextMap context_map = {};
  ConsentedDebugConfiguration config;
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, context_map));

  config.set_is_consented(true);
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, context_map));

  config.set_is_consented(false);
  config.set_token(kTestToken);
  EXPECT_FALSE(MaybeAddConsentedDebugConfig(config, context_map));
}

TEST(MaybeAddConsentedDebugConfig, AddConsentedDebugConfigToContext) {
  ContextImpl::ContextMap context_map = {};
  ConsentedDebugConfiguration config;
  config.set_is_consented(true);
  config.set_token(kTestToken);
  ContextImpl::ContextMap expected_map = {{kToken, kTestToken}};
  EXPECT_TRUE(MaybeAddConsentedDebugConfig(config, context_map));
  EXPECT_EQ(context_map, expected_map);
}

TEST(ConsentedTest, NotConsented_NoToken) {
  // default
  EXPECT_FALSE(ContextImpl({}, /*server_token=*/"").is_consented());
  // no client token
  EXPECT_FALSE(ContextImpl({}, kTestToken).is_consented());
  // empty server and client token
  EXPECT_FALSE(ContextImpl({{kToken, ""}},
                           /*server_token=*/"")
                   .is_consented());
  // empty client token, valid server token
  EXPECT_FALSE(ContextImpl({{kToken, ""}}, kTestToken).is_consented());
  // valid client token, empty server token
  EXPECT_FALSE(ContextImpl({{kToken, kTestToken}},
                           /*server_token=*/"")
                   .is_consented());
}

constexpr char kMismatchedToken[] = "mismatch_test";

TEST(ConsentedTest, NotConsented_Mismatch) {
  EXPECT_FALSE(
      ContextImpl({{kToken, kTestToken}}, kMismatchedToken).is_consented());
}

TEST(ConsentedTest, Consented) {
  EXPECT_TRUE(ContextImpl({{kToken, kTestToken}}, kTestToken).is_consented());
}

TEST(ConsentedTest, SetContext) {
  auto logger = ContextImpl({{kToken, kTestToken}}, kTestToken);
  EXPECT_TRUE(logger.is_consented());
  logger.UpdateContext({{}});
  EXPECT_FALSE(logger.is_consented());
  logger.UpdateContext({{kToken, kTestToken}});
  EXPECT_TRUE(logger.is_consented());
  logger.UpdateContext({{kToken, kMismatchedToken}});
  EXPECT_FALSE(logger.is_consented());
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
