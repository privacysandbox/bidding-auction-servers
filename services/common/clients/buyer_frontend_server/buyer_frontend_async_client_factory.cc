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

#include "absl/container/flat_hash_map.h"
#include "api/bidding_auction_servers.grpc.pb.h"
#include "services/common/clients/async_client.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"

namespace privacy_sandbox::bidding_auction_servers {

BuyerFrontEndAsyncClientFactory::BuyerFrontEndAsyncClientFactory(
    const absl::flat_hash_map<std::string, BuyerServiceEndpoint>&
        buyer_ig_owner_to_bfe_addr_map,
    server_common::KeyFetcherManagerInterface* key_fetcher_manager,
    CryptoClientWrapperInterface* crypto_client,
    const BuyerServiceClientConfig& client_config) {
  absl::flat_hash_map<std::string, std::shared_ptr<BuyerFrontEndAsyncClient>>
      bfe_addr_client_map;
  auto static_client_map = std::make_unique<absl::flat_hash_map<
      std::string, std::shared_ptr<BuyerFrontEndAsyncClient>>>();
  for (const auto& [ig_owner, bfe_endpoint] : buyer_ig_owner_to_bfe_addr_map) {
    // Can perform additional validations here.
    if (ig_owner.empty() || bfe_endpoint.endpoint.empty()) {
      continue;
    }

    if (auto it = bfe_addr_client_map.find(bfe_endpoint.endpoint);
        it != bfe_addr_client_map.end()) {
      static_client_map->try_emplace(ig_owner, it->second);
    } else {
      BuyerServiceClientConfig client_config_copy = client_config;
      client_config_copy.server_addr = bfe_endpoint.endpoint;
      client_config_copy.cloud_platform = bfe_endpoint.cloud_platform;
      auto bfe_client_ptr = std::make_shared<BuyerFrontEndAsyncGrpcClient>(
          key_fetcher_manager, crypto_client, std::move(client_config_copy));
      bfe_addr_client_map.insert({bfe_endpoint.endpoint, bfe_client_ptr});
      static_client_map->try_emplace(ig_owner, bfe_client_ptr);
    }
  }
  client_cache_ = std::move(static_client_map);

  for (const auto& [ig_owner, client] : *client_cache_) {
    entries_.push_back({ig_owner, client});
  }
}

std::shared_ptr<BuyerFrontEndAsyncClient> BuyerFrontEndAsyncClientFactory::Get(
    absl::string_view ig_owner) const {
  std::string ig_owner_str = absl::StrCat(ig_owner);
  if (auto it = client_cache_->find(ig_owner_str); it != client_cache_->end()) {
    return it->second;
  }

  return nullptr;
}

std::vector<
    std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
BuyerFrontEndAsyncClientFactory::Entries() const {
  return entries_;
}

}  // namespace privacy_sandbox::bidding_auction_servers
