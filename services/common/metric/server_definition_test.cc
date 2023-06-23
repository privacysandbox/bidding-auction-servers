//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "services/common/metric/server_definition.h"

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers::metric {
namespace {
TEST(Sever, Initialization) {
  server_common::metric::ServerConfig config_proto;
  config_proto.set_mode(server_common::metric::ServerConfig::PROD);
  BiddingContextMap(server_common::metric::BuildDependentConfig(config_proto));
  BfeContextMap(server_common::metric::BuildDependentConfig(config_proto));
  AuctionContextMap(server_common::metric::BuildDependentConfig(config_proto));
  SfeContextMap(server_common::metric::BuildDependentConfig(config_proto));
}

TEST(Sever, GetContext) {
  server_common::metric::ServerConfig config_proto;
  config_proto.set_mode(server_common::metric::ServerConfig::PROD);
  auto* bidding = BiddingContextMap(
      server_common::metric::BuildDependentConfig(config_proto));
  const GenerateBidsRequest request;
  EXPECT_FALSE(bidding->Get(&request).is_decrypted());
  bidding->Get(&request).SetDecrypted();
  EXPECT_TRUE(bidding->Get(&request).is_decrypted());
  CHECK_OK(bidding->Remove(&request));
  EXPECT_FALSE(bidding->Get(&request).is_decrypted());
}

}  // namespace

}  // namespace privacy_sandbox::bidding_auction_servers::metric
