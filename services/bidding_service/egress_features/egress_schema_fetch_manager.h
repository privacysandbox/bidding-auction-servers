/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_FETCH_MANAGER_H_
#define SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_FETCH_MANAGER_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/egress_features/adtech_schema_fetcher.h"
#include "services/bidding_service/egress_features/egress_schema_bucket_fetcher.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/bidding_service/egress_schema_fetch_config.pb.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "src/concurrent/executor.h"
#include "src/public/cpio/interface/blob_storage_client/blob_storage_client_interface.h"

namespace privacy_sandbox::bidding_auction_servers {

struct EgressSchemaCaches {
  std::unique_ptr<EgressSchemaCache> egress_schema_cache = nullptr;
  std::unique_ptr<EgressSchemaCache> unlimited_egress_schema_cache = nullptr;
};

// This class manages fetching and caching of egress schemas.
class EgressSchemaFetchManager {
 public:
  struct Options {
    bool enable_protected_app_signals = false;
    bool enable_temporary_unlimited_egress = false;
    int limited_egress_bits = 0;

    // All of the following fields must outlive EgressSchemaFetchManager.
    bidding_service::EgressSchemaFetchConfig fetch_config;
    server_common::Executor* executor;
    HttpFetcherAsync* http_fetcher_async;
    std::unique_ptr<google::scp::cpio::BlobStorageClientInterface>
        blob_storage_client;

    // May be moved from after calling EgressSchemaFetchManager::Init.
    // CDDL caches will be initialized on Init.
    std::unique_ptr<CddlSpecCache> temporary_unlimited_egress_cddl_cache;
    std::unique_ptr<CddlSpecCache> egress_cddl_cache;
  };

  // Creates an instance of EgressSchemaFetchManager.
  //
  // `options`: Mutable wrapper containing all dependencies and config.
  // Will be owned by EgressSchemaFetchManager for its lifetime.
  explicit EgressSchemaFetchManager(Options options)
      : options_(std::move(options)) {}

  // EgressSchemaFetchManager is neither copyable nor movable.
  EgressSchemaFetchManager(const EgressSchemaFetchManager&) = delete;
  EgressSchemaFetchManager& operator=(const EgressSchemaFetchManager&) = delete;

  // Configures the runtime versioning defaults to be used by reactors.
  // May be called regardless of the status of Init().
  // May modify runtime_config.
  absl::Status ConfigureRuntimeDefaults(
      BiddingServiceRuntimeConfig& runtime_config);

  // Must be called exactly once. Failure to Init means that the bidding service
  // does not have any egress schema caches.
  absl::StatusOr<EgressSchemaCaches> Init();

 private:
  struct StartEgressSchemaFetchResult {
    std::unique_ptr<EgressSchemaCache> schema_cache;
    std::unique_ptr<FetcherInterface> schema_fetcher;
  };

  absl::StatusOr<StartEgressSchemaFetchResult> StartEgressSchemaFetch(
      absl::string_view url, absl::string_view bucket,
      std::unique_ptr<CddlSpecCache> cddl_spec_cache);

  absl::Status InitializeBucketClient();
  Options options_;

  std::unique_ptr<FetcherInterface> egress_schema_fetcher_;
  std::unique_ptr<FetcherInterface> unlimited_egress_schema_fetcher_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_BIDDING_SERVICE_EGRESS_SCHEMA_FETCH_MANAGER_H_
