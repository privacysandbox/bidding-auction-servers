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

#ifndef SERVICES_COMMON_CLIENTS_BUYER_FRONTEND_SERVER_BUYER_FRONTEND_ASYNC_CLIENT_FACTORY_H_
#define SERVICES_COMMON_CLIENTS_BUYER_FRONTEND_SERVER_BUYER_FRONTEND_ASYNC_CLIENT_FACTORY_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "services/common/clients/buyer_frontend_server/buyer_frontend_async_client.h"
#include "services/common/clients/client_factory.h"
#include "services/common/concurrent/local_cache.h"
#include "services/seller_frontend_service/util/config_param_parser.h"
#include "src/encryption/key_fetcher/key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

// Constructs and returns a BuyerFrontEndAsyncClient.
class BuyerFrontEndAsyncClientFactory
    : public ClientFactory<BuyerFrontEndAsyncClient, absl::string_view> {
 public:
  // Creates a new BuyerFrontEndAsyncClientFactory with pre-warmed cache of
  // clients for the hosts in host_addr_map.
  // Map is keyed on IG Owner (Buyer domain) and value corresponds to BFE host
  // domain. If compression is enabled, uses the GZIP algorithm to compress
  // requests to BuyerFrontEnd Server. Compression is disabled by default.
  // TLS is enabled by default.
  explicit BuyerFrontEndAsyncClientFactory(
      const absl::flat_hash_map<std::string, BuyerServiceEndpoint>&
          buyer_ig_owner_to_bfe_addr_map,
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client,
      const BuyerServiceClientConfig& client_config);

  // BuyerFrontEndAsyncClientFactory is neither copyable nor movable.
  BuyerFrontEndAsyncClientFactory(BuyerFrontEndAsyncClientFactory&) = delete;
  BuyerFrontEndAsyncClientFactory& operator=(BuyerFrontEndAsyncClientFactory&) =
      delete;

  // Provides a shared pointer to the BuyerFrontEndAsyncClient for the BFE
  // operated by the buyer specified via ig_owner, IF AND ONLY IF that client
  // is in the cache.
  // Else returns nullptr.
  // Shared pointer is used so that the lifetime of the client is independent
  // of the lifetime of this factory or cache in this factory.
  std::shared_ptr<BuyerFrontEndAsyncClient> Get(
      absl::string_view ig_owner) const override;

  // Returns a list of all <buyer_domain, bfe_client> pairs contained by the
  // factory.
  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
  Entries() const override;

 private:
  std::unique_ptr<absl::flat_hash_map<
      std::string, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      client_cache_;

  std::vector<
      std::pair<absl::string_view, std::shared_ptr<BuyerFrontEndAsyncClient>>>
      entries_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_BUYER_FRONTEND_SERVER_BUYER_FRONTEND_ASYNC_CLIENT_FACTORY_H_
