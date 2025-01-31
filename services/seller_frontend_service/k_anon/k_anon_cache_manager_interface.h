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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_INTERFACE_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_INTERFACE_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "services/common/clients/k_anon_server/k_anon_client.h"
#include "services/common/metric/server_definition.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

// Configurable inputs given to KAnonCacheManager.
struct KAnonCacheManagerConfig {
  int total_num_hash = 1000;
  double expected_k_anon_to_non_k_anon_ratio = 1.0;
  absl::Duration client_time_out = absl::Milliseconds(60000);
  int num_k_anon_shards = 1;
  int num_non_k_anon_shards = 1;
  absl::Duration k_anon_ttl = absl::Hours(24);
  absl::Duration non_k_anon_ttl = absl::Hours(3);
  bool enable_k_anon_cache = true;
};

// Interface for KAnonCacheManager.
//
// KAnonCacheManager is responsible for caching k-anon hashes and querying the
// k-anon service to determine which hashes are k-anon and which aren't.
class KAnonCacheManagerInterface {
 public:
  using Callback = absl::AnyInvocable<void(
      absl::StatusOr<absl::flat_hash_set<std::string>>) const>;
  KAnonCacheManagerInterface() = default;
  virtual ~KAnonCacheManagerInterface() = default;
  explicit KAnonCacheManagerInterface(
      server_common::Executor* executor,
      std::unique_ptr<KAnonGrpcClientInterface> k_anon_client = nullptr,
      KAnonCacheManagerConfig config = {}) {}

  // Updates and returns k-anon hash of each passed in hash.
  // Queries caches and optionally the k-anon service to determine which hashes
  // are k-anon and which aren't.
  virtual absl::Status AreKAnonymous(
      absl::string_view set_type, absl::flat_hash_set<absl::string_view> hashes,
      Callback callback, metric::SfeContext* sfe_metric_context) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_MANAGER_INTERFACE_H_
