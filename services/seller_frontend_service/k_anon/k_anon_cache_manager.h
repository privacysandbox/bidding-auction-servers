//  Copyright 2024 Google LLC
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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/common/clients/k_anon_server/k_anon_client.h"
#include "services/common/metric/server_definition.h"
#include "services/seller_frontend_service/cache/cache.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

class KAnonCacheManager : public KAnonCacheManagerInterface {
 public:
  using ShardedKAnonCache =
      std::vector<std::unique_ptr<CacheInterface<std::string, std::string>>>;

  explicit KAnonCacheManager(
      server_common::Executor* executor,
      std::unique_ptr<KAnonGrpcClientInterface> k_anon_client,
      KAnonCacheManagerConfig config = {});

  // Constructor used to test with MockCache.
  explicit KAnonCacheManager(
      ShardedKAnonCache k_anon_caches, ShardedKAnonCache non_k_anon_caches,
      std::unique_ptr<KAnonGrpcClientInterface> k_anon_client,
      KAnonCacheManagerConfig config = {});

  ~KAnonCacheManager() = default;

  // Updates and returns k-anon hash of each passed in hash.
  // Queries caches and optionally the k-anon service to determine which hashes
  // are k-anon and which aren't.
  absl::Status AreKAnonymous(absl::string_view set_type,
                             absl::flat_hash_set<absl::string_view> hashes,
                             Callback callback,
                             metric::SfeContext* sfe_metric_context) override;

 protected:
  // Queries k-anon client for k-anon hashes.
  absl::Status QueryKAnonClient(
      absl::flat_hash_set<absl::string_view> unresolved_hashes,
      absl::string_view set_type, Callback callback,
      absl::flat_hash_set<std::string> resolved_k_anon_hashes,
      metric::SfeContext* sfe_metric_context);

  // Maps given hashes to the right sharded cache.
  std::vector<absl::flat_hash_set<std::string>> MapHashToShardedCache(
      const absl::flat_hash_set<absl::string_view>& hashes, int num_shards);

  // Queries ShardedKAnonCache using MapHashToShardedCache and returns a merged
  // set of hashes returned by ShardedKAnonCache.
  absl::flat_hash_set<std::string> QueryShardedCaches(
      const ShardedKAnonCache& caches, int num_shards,
      const absl::flat_hash_set<absl::string_view>& hashes);

  // Inserts hashes into ShardedKAnonCache using MapHashToShardedCache. The
  // statuses returned by the cache is logged, but not returend.
  void InsertShardedCaches(
      const ShardedKAnonCache& caches, int num_shards,
      const absl::flat_hash_set<absl::string_view>& hashes);

 private:
  // Caches for hashes that previously resolved to k-anon status.
  ShardedKAnonCache k_anon_caches_;

  // Caches for hashes that previously resolved to non-k-anon status.
  ShardedKAnonCache non_k_anon_caches_;

  std::unique_ptr<KAnonGrpcClientInterface> k_anon_client_;

  // Used to time out of k-anon client's Execute() which queries for k-anon
  // hashes.
  absl::Duration client_time_out_;

  // Number of k-anon shard caches.
  int num_k_anon_shards_;

  // Number of non-k-anon shard caches.
  int num_non_k_anon_shards_;

  // Flag to make k-anon cache query optional. If set to false, k-anon caches
  // will not be queried.
  bool enable_k_anon_cache_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_H_
