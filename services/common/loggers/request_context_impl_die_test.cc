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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/loggers/request_context_impl.h"

namespace privacy_sandbox::bidding_auction_servers::log {
namespace {

TEST(ServerToken, DieWithShortToken) {
  absl::string_view short_token = "123";
  EXPECT_DEATH(ServerToken(short_token),
               "server token length must be at least");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::log
