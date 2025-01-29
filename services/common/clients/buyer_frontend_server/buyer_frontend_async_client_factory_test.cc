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

#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client_factory.h"

#include <utility>

#include "gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::testing::Return;

TEST(BuyerFrontEndAsyncClientFactoryTest, GetCachesClientObjects) {
  std::string ig_owner = MakeARandomString();
  std::string bfe_lb_url = MakeARandomString();
  BuyerServiceEndpoint endpoint = {bfe_lb_url,
                                   server_common::CloudPlatform::kGcp};

  absl::flat_hash_map<std::string, BuyerServiceEndpoint> host_addr_map;
  host_addr_map.emplace(ig_owner, endpoint);
  BuyerFrontEndAsyncClientFactory class_under_test(
      host_addr_map, nullptr, nullptr, {.ca_root_pem = kTestCaCertPath});

  EXPECT_EQ(class_under_test.Get(ig_owner).get(),
            class_under_test.Get(ig_owner).get());
}

TEST(BuyerFrontEndAsyncClientFactoryTest, GetReturnsNonNullClient) {
  std::string ig_owner = MakeARandomString();
  std::string bfe_lb_url = MakeARandomString();
  BuyerServiceEndpoint endpoint = {bfe_lb_url,
                                   server_common::CloudPlatform::kGcp};

  absl::flat_hash_map<std::string, BuyerServiceEndpoint> host_addr_map;
  host_addr_map.emplace(ig_owner, endpoint);
  BuyerFrontEndAsyncClientFactory class_under_test(
      host_addr_map, nullptr, nullptr, {.ca_root_pem = kTestCaCertPath});

  std::shared_ptr<const BuyerFrontEndAsyncClient> output =
      class_under_test.Get(ig_owner);
  EXPECT_NE(output.get(), nullptr);
}

TEST(BuyerFrontEndAsyncClientFactoryTest,
     CreatesSharedClientsForSameHostAddress) {
  std::string ig_owner_1 = MakeARandomString();
  std::string ig_owner_2 = MakeARandomString();
  std::string bfe_lb_url = MakeARandomString();
  BuyerServiceEndpoint endpoint = {bfe_lb_url,
                                   server_common::CloudPlatform::kGcp};

  absl::flat_hash_map<std::string, BuyerServiceEndpoint> host_addr_map;
  host_addr_map.emplace(ig_owner_1, endpoint);
  host_addr_map.emplace(ig_owner_2, endpoint);
  BuyerFrontEndAsyncClientFactory class_under_test(
      host_addr_map, nullptr, nullptr, {.ca_root_pem = kTestCaCertPath});

  std::shared_ptr<const BuyerFrontEndAsyncClient> output_1 =
      class_under_test.Get(ig_owner_1);
  std::shared_ptr<const BuyerFrontEndAsyncClient> output_2 =
      class_under_test.Get(ig_owner_2);
  EXPECT_EQ(output_1, output_2);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
