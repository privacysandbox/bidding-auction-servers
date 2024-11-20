/*_
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

#include "services/bidding_service/egress_features/egress_schema_fetch_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "services/bidding_service/cddl_spec_cache.h"
#include "services/bidding_service/constants.h"
#include "services/bidding_service/data/runtime_config.h"
#include "services/bidding_service/egress_features/adtech_schema_fetcher.h"
#include "services/bidding_service/egress_schema_cache.h"
#include "services/bidding_service/egress_schema_fetch_config.pb.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/data_fetch/version_util.h"
#include "services/common/loggers/request_log_context.h"
#include "src/concurrent/executor.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

absl::StatusOr<EgressSchemaCaches> EgressSchemaFetchManager::Init() {
  EgressSchemaCaches caches;
  if (!options_.enable_protected_app_signals) {
    return caches;
  }

  if (options_.fetch_config.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET) {
    PS_RETURN_IF_ERROR(InitializeBucketClient());
  }

  if (options_.enable_temporary_unlimited_egress) {
    PS_LOG(INFO) << "Temporary egress feature is enabled in the binary, "
                    "attempting schema fetch...";
    PS_ASSIGN_OR_RETURN(
        auto temporary_egress_cache_result,
        StartEgressSchemaFetch(
            options_.fetch_config.temporary_unlimited_egress_schema_url(),
            options_.fetch_config.temporary_unlimited_egress_schema_bucket(),
            std::move(options_.temporary_unlimited_egress_cddl_cache)),
        _ << "Temporary egress schema fetch unsuccessful.");
    PS_LOG(INFO) << "Temporary egress schema fetch successful.";
    unlimited_egress_schema_fetcher_ =
        std::move(temporary_egress_cache_result.schema_fetcher);
    caches.unlimited_egress_schema_cache =
        std::move(temporary_egress_cache_result.schema_cache);
  } else {
    PS_LOG(INFO) << "Temporary egress feature is not enabled in the binary";
  }

  const bool egress_enabled = options_.limited_egress_bits > 0;
  if (egress_enabled) {
    PS_LOG(INFO) << "Limited egress feature is enabled in the binary, "
                    "attempting schema fetch...";
    PS_ASSIGN_OR_RETURN(
        auto egress_cache_result,
        StartEgressSchemaFetch(options_.fetch_config.egress_schema_url(),
                               options_.fetch_config.egress_schema_bucket(),
                               std::move(options_.egress_cddl_cache)),
        _ << "Limited egress schema fetch unsuccessful.");
    PS_LOG(INFO) << "Limited egress schema fetch successful.";
    egress_schema_fetcher_ = std::move(egress_cache_result.schema_fetcher);
    caches.egress_schema_cache = std::move(egress_cache_result.schema_cache);
  } else {
    PS_LOG(INFO) << "Limited egress feature is not enabled in the binary";
  }

  return caches;
}

absl::Status EgressSchemaFetchManager::InitializeBucketClient() {
  PS_RETURN_IF_ERROR(options_.blob_storage_client->Init()).SetPrepend()
      << "Egress blob storage client init failed.";
  PS_RETURN_IF_ERROR(options_.blob_storage_client->Run()).SetPrepend()
      << "Egress blob storage client run failed.";
  return absl::OkStatus();
}

absl::StatusOr<EgressSchemaFetchManager::StartEgressSchemaFetchResult>
EgressSchemaFetchManager::StartEgressSchemaFetch(
    absl::string_view url, absl::string_view bucket,
    std::unique_ptr<CddlSpecCache> cddl_spec_cache) {
  PS_RETURN_IF_ERROR(cddl_spec_cache->Init())
      << "Unable to init cddl spec cache.";
  auto egress_schema_cache =
      std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache));

  std::unique_ptr<FetcherInterface> adtech_schema_fetcher;
  if (options_.fetch_config.fetch_mode() == blob_fetch::FETCH_MODE_URL) {
    adtech_schema_fetcher = std::make_unique<AdtechSchemaFetcher>(
        std::vector<std::string>{std::string(url)},
        absl::Milliseconds(options_.fetch_config.url_fetch_period_ms()),
        absl::Milliseconds(options_.fetch_config.url_fetch_timeout_ms()),
        options_.http_fetcher_async, options_.executor,
        egress_schema_cache.get());
  } else if (options_.fetch_config.fetch_mode() ==
             blob_fetch::FETCH_MODE_BUCKET) {
    adtech_schema_fetcher = std::make_unique<EgressSchemaBucketFetcher>(
        bucket, absl::Milliseconds(options_.fetch_config.url_fetch_period_ms()),
        options_.executor, options_.blob_storage_client.get(),
        egress_schema_cache.get());

  } else {
    return absl::InvalidArgumentError(
        "Egress schema fetch mode not supported.");
  }

  PS_RETURN_IF_ERROR(adtech_schema_fetcher->Start())
      << "Unable to start fetching the egress schema.";

  return EgressSchemaFetchManager::StartEgressSchemaFetchResult(
      {.schema_cache = std::move(egress_schema_cache),
       .schema_fetcher = std::move(adtech_schema_fetcher)});
}

absl::Status EgressSchemaFetchManager::ConfigureRuntimeDefaults(
    BiddingServiceRuntimeConfig& runtime_config) {
  if (!options_.enable_protected_app_signals) {
    return absl::OkStatus();
  }
  if (options_.fetch_config.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET) {
    runtime_config.use_per_request_schema_versioning = true;
    PS_ASSIGN_OR_RETURN(
        runtime_config.default_egress_schema_version,
        GetBucketBlobVersion(
            options_.fetch_config.egress_schema_bucket(),
            options_.fetch_config.egress_default_schema_in_bucket()));
    PS_ASSIGN_OR_RETURN(
        runtime_config.default_unlimited_egress_schema_version,
        GetBucketBlobVersion(
            options_.fetch_config.temporary_unlimited_egress_schema_bucket(),
            options_.fetch_config
                .temporary_unlimited_egress_default_schema_in_bucket()));
  } else {
    runtime_config.use_per_request_schema_versioning = false;
    runtime_config.default_egress_schema_version = kDefaultEgressSchemaId;
    runtime_config.default_unlimited_egress_schema_version =
        kDefaultEgressSchemaId;
  }
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
