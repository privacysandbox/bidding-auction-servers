/*
 * Copyright 2024 Google LLC
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

#include "gtest/gtest.h"
#include "services/common/loggers/request_log_context.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {

TEST(RequestLogContext, Initialization) {
  RequestLogContext context({}, server_common::ConsentedDebugConfiguration());
}

TEST(RequestLogContext, NoOpContext) {
  RequestContext context = NoOpContext();
  EXPECT_FALSE(context.log.is_consented());
  EXPECT_FALSE(context.log.is_debug_response());

  PS_LOG(INFO, context.log) << "NoOpContext";
  context.log.SetEventMessageField("NoOpContext EventMessage");
  RequestContext context2 = NoOpContext();
  EXPECT_EQ(&context.log, &context2.log);
}

TEST(RequestLogContext, ModifyConsentCannotChange) {
  CommonTestInit();
  // set server token
  server_common::log::ServerToken("123456");

  server_common::ConsentedDebugConfiguration config;
  config.set_is_consented(true);
  config.set_token("not_equal");

  ModifyConsent(config);
  EXPECT_EQ(config.token(), "not_equal");
}

}  // namespace privacy_sandbox::bidding_auction_servers
