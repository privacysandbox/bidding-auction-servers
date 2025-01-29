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

absl::StatusOr<EgressSchemaCaches> EgressSchemaFetchManager::Init(
    BiddingServiceRuntimeConfig& runtime_config) {
  EgressSchemaCaches caches;
  if (!options_.enable_protected_app_signals) {
    return caches;
  }
  if (options_.fetch_config.empty()) {
    return absl::InvalidArgumentError(kNoEgressSchemaFetchConfig);
  }
  PS_RETURN_IF_ERROR(google::protobuf::util::JsonStringToMessage(
      options_.fetch_config.data(), &fetch_config_))
      << kParseEgressSchemaFetchConfigFailure;

  PS_LOG(INFO) << "Fetched egress schema fetch config: "
               << fetch_config_.DebugString();

  PS_RETURN_IF_ERROR(ConfigureRuntimeDefaults(runtime_config));

  if (fetch_config_.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET) {
    PS_RETURN_IF_ERROR(InitializeBucketClient());
  }

  if (options_.enable_temporary_unlimited_egress) {
    PS_LOG(INFO) << kTemporaryEgressEnabled;
    PS_ASSIGN_OR_RETURN(
        auto temporary_egress_cache_result,
        StartEgressSchemaFetch(
            fetch_config_.temporary_unlimited_egress_schema_url(),
            fetch_config_.temporary_unlimited_egress_schema_bucket(),
            std::move(options_.temporary_unlimited_egress_cddl_cache)),
        _ << kTemporaryEgressFetchUnsuccessful);
    PS_LOG(INFO) << kTemporaryEgressFetchSuccessful;
    unlimited_egress_schema_fetcher_ =
        std::move(temporary_egress_cache_result.schema_fetcher);
    caches.unlimited_egress_schema_cache =
        std::move(temporary_egress_cache_result.schema_cache);
  } else {
    PS_LOG(INFO) << kTemporaryEgressDisabled;
  }

  const bool egress_enabled = options_.limited_egress_bits > 0;
  if (egress_enabled) {
    PS_LOG(INFO) << options_.limited_egress_bits << kLimitedEgressEnabled;
    PS_ASSIGN_OR_RETURN(
        auto egress_cache_result,
        StartEgressSchemaFetch(fetch_config_.egress_schema_url(),
                               fetch_config_.egress_schema_bucket(),
                               std::move(options_.egress_cddl_cache)),
        _ << kLimitedEgressFetchUnsuccessful);
    PS_LOG(INFO) << kLimitedEgressFetchSuccessful;
    egress_schema_fetcher_ = std::move(egress_cache_result.schema_fetcher);
    caches.egress_schema_cache = std::move(egress_cache_result.schema_cache);
  } else {
    PS_LOG(INFO) << kLimitedEgressDisabled;
  }

  return caches;
}

const bidding_service::EgressSchemaFetchConfig&
EgressSchemaFetchManager::GetFetchConfig() const {
  return fetch_config_;
}

absl::Status EgressSchemaFetchManager::InitializeBucketClient() {
  PS_RETURN_IF_ERROR(options_.blob_storage_client->Init()).SetPrepend()
      << kEgressBlobStorageClientInitFailure;
  PS_RETURN_IF_ERROR(options_.blob_storage_client->Run()).SetPrepend()
      << kEgressBlobStorageClientRunFailure;
  return absl::OkStatus();
}

absl::StatusOr<EgressSchemaFetchManager::StartEgressSchemaFetchResult>
EgressSchemaFetchManager::StartEgressSchemaFetch(
    absl::string_view url, absl::string_view bucket,
    std::unique_ptr<CddlSpecCache> cddl_spec_cache) {
  PS_RETURN_IF_ERROR(cddl_spec_cache->Init()) << kCddlSpecCacheInitFailure;
  auto egress_schema_cache =
      std::make_unique<EgressSchemaCache>(std::move(cddl_spec_cache));

  std::unique_ptr<FetcherInterface> adtech_schema_fetcher;
  if (fetch_config_.fetch_mode() == blob_fetch::FETCH_MODE_URL) {
    adtech_schema_fetcher = std::make_unique<AdtechSchemaFetcher>(
        std::vector<std::string>{std::string(url)},
        absl::Milliseconds(fetch_config_.url_fetch_period_ms()),
        absl::Milliseconds(fetch_config_.url_fetch_timeout_ms()),
        options_.http_fetcher_async, options_.executor,
        egress_schema_cache.get());
  } else if (fetch_config_.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET) {
    adtech_schema_fetcher = std::make_unique<EgressSchemaBucketFetcher>(
        bucket, absl::Milliseconds(fetch_config_.url_fetch_period_ms()),
        options_.executor, options_.blob_storage_client.get(),
        egress_schema_cache.get());

  } else {
    return absl::InvalidArgumentError(kEgressSchemaFetchModeNotSupported);
  }

  PS_RETURN_IF_ERROR(adtech_schema_fetcher->Start())
      << kEgressSchemaFetchStartFailure;

  return EgressSchemaFetchManager::StartEgressSchemaFetchResult(
      {.schema_cache = std::move(egress_schema_cache),
       .schema_fetcher = std::move(adtech_schema_fetcher)});
}

absl::Status EgressSchemaFetchManager::ConfigureRuntimeDefaults(
    BiddingServiceRuntimeConfig& runtime_config) {
  if (options_.enable_temporary_unlimited_egress == false &&
      options_.limited_egress_bits < 1) {
    return absl::InvalidArgumentError(kMustUseTemporaryOrLimitedEgress);
  }
  if (fetch_config_.fetch_mode() == blob_fetch::FETCH_MODE_BUCKET) {
    runtime_config.use_per_request_schema_versioning = true;
    if (options_.enable_temporary_unlimited_egress) {
      PS_ASSIGN_OR_RETURN(
          runtime_config.default_unlimited_egress_schema_version,
          GetBucketBlobVersion(
              fetch_config_.temporary_unlimited_egress_schema_bucket(),
              fetch_config_
                  .temporary_unlimited_egress_default_schema_in_bucket()),
          _ << kNoUnlimitedSchemaVersion);
    }
    if (options_.limited_egress_bits > 0) {
      PS_ASSIGN_OR_RETURN(
          runtime_config.default_egress_schema_version,
          GetBucketBlobVersion(fetch_config_.egress_schema_bucket(),
                               fetch_config_.egress_default_schema_in_bucket()),
          _ << kNoLimitedSchemaVersion);
    }
  } else {
    runtime_config.use_per_request_schema_versioning = false;
    runtime_config.default_egress_schema_version = kDefaultEgressSchemaId;
    runtime_config.default_unlimited_egress_schema_version =
        kDefaultEgressSchemaId;
  }
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
