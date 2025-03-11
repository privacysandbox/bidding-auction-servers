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

#include "services/seller_frontend_service/k_anon/k_anon_cache_manager.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/internal/endian.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "src/logger/request_context_logger.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using ::google::chrome::kanonymityquery::v1::ValidateHashesRequest;
using ::google::chrome::kanonymityquery::v1::ValidateHashesResponse;

std::function<std::string(const std::string&, const std::string&)>
GetEntryStringifyFunc() {
  static auto func = [](const std::string& key, const std::string& value) {
    return absl::StrCat("[key: ", absl::BytesToHexString(key), "]");
  };

  return func;
}

inline int GetShardIndex(absl::string_view hash, int num_shards) {
  return absl::little_endian::Load32(hash.data()) % num_shards;
}

}  // namespace

KAnonCacheManager::KAnonCacheManager(
    server_common::Executor* executor,
    std::unique_ptr<KAnonGrpcClientInterface> k_anon_client,
    KAnonCacheManagerConfig config)
    : KAnonCacheManagerInterface(
          executor, std::unique_ptr<KAnonGrpcClientInterface>(nullptr), config),
      k_anon_client_(std::move(k_anon_client)),
      client_time_out_(config.client_time_out),
      num_k_anon_shards_(config.num_k_anon_shards),
      num_non_k_anon_shards_(config.num_non_k_anon_shards),
      enable_k_anon_cache_(config.enable_k_anon_cache) {
  if (!enable_k_anon_cache_) {
    return;
  }
  CHECK_GT(config.num_k_anon_shards, 0);
  CHECK_GT(config.num_non_k_anon_shards, 0);

  double expected_ratio = config.expected_k_anon_to_non_k_anon_ratio;
  int k_anon_shards = config.num_k_anon_shards;
  int non_k_anon_shards = config.num_non_k_anon_shards;
  int k_anon_cache_capacity = std::round(
      (config.total_num_hash * (expected_ratio / (expected_ratio + 1.0))) /
      k_anon_shards);
  int non_k_anon_cache_capacity =
      std::round((config.total_num_hash * (1.0 / (expected_ratio + 1.0))) /
                 non_k_anon_shards);

  ShardedKAnonCache k_anon_caches;
  ShardedKAnonCache non_k_anon_caches;
  k_anon_caches.reserve(k_anon_shards);
  non_k_anon_caches.reserve(non_k_anon_shards);
  for (int i = 0; i < k_anon_shards; i++) {
    k_anon_caches.emplace_back(
        std::make_unique<Cache<std::string, std::string>>(
            /* capacity= */ k_anon_cache_capacity, config.k_anon_ttl, executor,
            GetEntryStringifyFunc()));
  }
  for (int i = 0; i < non_k_anon_shards; i++) {
    non_k_anon_caches.emplace_back(
        std::make_unique<Cache<std::string, std::string>>(
            /* capacity= */ non_k_anon_cache_capacity, config.non_k_anon_ttl,
            executor, GetEntryStringifyFunc()));
  }
  k_anon_caches_ = std::move(k_anon_caches);
  non_k_anon_caches_ = std::move(non_k_anon_caches);
}

KAnonCacheManager::KAnonCacheManager(
    ShardedKAnonCache k_anon_caches, ShardedKAnonCache non_k_anon_caches,
    std::unique_ptr<KAnonGrpcClientInterface> k_anon_client,
    KAnonCacheManagerConfig config)
    : KAnonCacheManagerInterface(nullptr, nullptr, {}),
      k_anon_caches_(std::move(k_anon_caches)),
      non_k_anon_caches_(std::move(non_k_anon_caches)),
      k_anon_client_(std::move(k_anon_client)),
      client_time_out_(config.client_time_out),
      num_k_anon_shards_(config.num_k_anon_shards),
      num_non_k_anon_shards_(config.num_non_k_anon_shards),
      enable_k_anon_cache_(config.enable_k_anon_cache) {
  num_k_anon_shards_ = k_anon_caches_.size();
  num_non_k_anon_shards_ = non_k_anon_caches_.size();
}

std::vector<absl::flat_hash_set<std::string>>
KAnonCacheManager::MapHashToShardedCache(
    const absl::flat_hash_set<absl::string_view>& hashes, int num_shards) {
  std::vector<absl::flat_hash_set<std::string>> mapped_hashes(num_shards);
  for (const auto& hash : hashes) {
    mapped_hashes[GetShardIndex(hash, num_shards)].insert(std::string(hash));
    PS_VLOG(5) << absl::BytesToHexString(hash) << " mapped to cache at index: "
               << GetShardIndex(hash, num_shards) << " out of " << num_shards
               << " shards";
  }
  return mapped_hashes;
}

absl::flat_hash_set<std::string> KAnonCacheManager::QueryShardedCaches(
    const ShardedKAnonCache& caches, int num_shards,
    const absl::flat_hash_set<absl::string_view>& hashes) {
  if (num_shards == 1) {
    return MapToSet(caches[0]->Query(
        absl::flat_hash_set<std::string>(hashes.begin(), hashes.end())));
  }
  absl::flat_hash_set<std::string> resolved_hashes;
  std::vector<absl::flat_hash_set<std::string>> mapped_hashes =
      MapHashToShardedCache(hashes, num_shards);
  for (int i = 0; i < mapped_hashes.size(); i++) {
    auto queried = MapToSet(caches[i]->Query(mapped_hashes[i]));
    resolved_hashes.insert(queried.begin(), queried.end());
  }
  return resolved_hashes;
}

void KAnonCacheManager::InsertShardedCaches(
    const ShardedKAnonCache& caches, int num_shards,
    const absl::flat_hash_set<absl::string_view>& hashes) {
  if (num_shards == 1) {
    absl::Status status = caches[0]->Insert(SetToMap(
        absl::flat_hash_set<std::string>(hashes.begin(), hashes.end())));
    if (!status.ok()) {
      PS_VLOG(5) << "Failed to update a cache with a status: " << status;
    }
    return;
  }
  std::vector<absl::flat_hash_set<std::string>> mapped_hashes =
      MapHashToShardedCache(hashes, num_shards);
  for (int i = 0; i < mapped_hashes.size(); i++) {
    absl::Status status = caches[i]->Insert(SetToMap(mapped_hashes[i]));
    if (!status.ok()) {
      PS_VLOG(5) << "Failed to update sharded k-anon cache at index " << i
                 << " with a status: " << status;
    }
  }
}

absl::Status KAnonCacheManager::QueryKAnonClient(
    absl::flat_hash_set<absl::string_view> unresolved_hashes,
    absl::string_view set_type, Callback callback,
    absl::flat_hash_set<std::string> resolved_k_anon_hashes,
    metric::SfeContext* sfe_metric_context) {
  auto request = std::make_unique<ValidateHashesRequest>();
  auto* type_sets_map = request->add_sets();
  type_sets_map->set_type(set_type);
  for (auto& hash : unresolved_hashes) {
    type_sets_map->add_hashes(hash);
  }
  auto k_anon_initiated_request =
      metric::MakeInitiatedRequest(metric::kKAnon, sfe_metric_context);
  k_anon_initiated_request->SetRequestSize((int)request->ByteSizeLong());
  auto on_done_execute =
      [this, unresolved_hashes = std::move(unresolved_hashes), set_type,
       callback = std::move(callback),
       resolved_k_anon_hashes = std::move(resolved_k_anon_hashes),
       k_anon_initiated_request = std::move(k_anon_initiated_request),
       sfe_metric_context](
          absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>
              response) mutable {
        if (!response.ok()) {
          PS_VLOG(5) << "k-anon query returned an error: " << response.status();
          LogIfError(sfe_metric_context->AccumulateMetric<
                     metric::kSfeInitiatedRequestKanonErrorCountByStatus>(
              1, (StatusCodeToString(response.status().code()))));
          callback(resolved_k_anon_hashes);
          return;
        }

        std::unique_ptr<ValidateHashesResponse> query_sets_response =
            *std::move(response);
        k_anon_initiated_request->SetResponseSize(
            (int)query_sets_response->ByteSizeLong());
        // Record the response time as soon as possible.
        k_anon_initiated_request.reset();

        PS_VLOG(5) << "Received k-anon hash response: "
                   << query_sets_response->DebugString();
        if (query_sets_response->k_anonymous_sets().empty()) {
          PS_VLOG(5) << "No k-anon sets returned by k-anon service";
          callback(std::move(resolved_k_anon_hashes));
          return;
        }
        absl::flat_hash_set<std::string> resolved;
        for (auto& type_sets_map :
             *query_sets_response->mutable_k_anonymous_sets()) {
          if (type_sets_map.type() != set_type) {
            PS_VLOG(5) << "Expected only one set type (" << set_type
                       << ") for this SelectAd request, got unexpected type: "
                       << type_sets_map.type() << ")";
            continue;
          }
          if (type_sets_map.hashes().empty()) {
            continue;
          }
          for (auto& hash : *type_sets_map.mutable_hashes()) {
            resolved.insert(std::move(hash));
          }
        }

        PS_VLOG(5)
            << "Success executing query k-anon service, the following hashes "
               "were returned from the client: "
            << KAnonHashSetsToString(resolved);
        resolved.insert(resolved_k_anon_hashes.begin(),
                        resolved_k_anon_hashes.end());
        callback(resolved);
        if (!enable_k_anon_cache_) {
          return;
        }
        InsertShardedCaches(k_anon_caches_, num_k_anon_shards_,
                            absl::flat_hash_set<absl::string_view>(
                                resolved.begin(), resolved.end()));
        absl::flat_hash_set<absl::string_view> resolved_non_k_anon_hashes;
        for (auto& hash : unresolved_hashes) {
          if (!resolved.contains(hash)) {
            resolved_non_k_anon_hashes.insert(hash);
          }
        }
        InsertShardedCaches(non_k_anon_caches_, num_non_k_anon_shards_,
                            resolved_non_k_anon_hashes);
      };

  return k_anon_client_->Execute(std::move(request), std::move(on_done_execute),
                                 client_time_out_);
}

absl::Status KAnonCacheManager::AreKAnonymous(
    absl::string_view set_type, absl::flat_hash_set<absl::string_view> hashes,
    Callback callback, metric::SfeContext* sfe_metric_context) {
  PS_VLOG(5) << " " << __func__ << " hashes to be queried for k-anon status: "
             << KAnonHashSetsToString(hashes);

  if (!enable_k_anon_cache_) {
    PS_RETURN_IF_ERROR(QueryKAnonClient(hashes, set_type, std::move(callback),
                                        {}, sfe_metric_context));
    return absl::OkStatus();
  }

  absl::flat_hash_set<std::string> k_anon_hashes =
      QueryShardedCaches(k_anon_caches_, num_k_anon_shards_, hashes);
  PS_VLOG(5) << "Received k-anon hashes from k-anon cache: "
             << KAnonHashSetsToString(k_anon_hashes);
  absl::flat_hash_set<std::string> non_k_anon_hashes =
      QueryShardedCaches(non_k_anon_caches_, num_non_k_anon_shards_, hashes);
  PS_VLOG(5) << "Received non k-anon hashes from non k-anon cache: "
             << KAnonHashSetsToString(non_k_anon_hashes);

  int num_total_hashes_to_resolve = hashes.size();
  int num_resolved_k_anon_hashes = k_anon_hashes.size();
  int num_resolved_non_k_anon_hashes = non_k_anon_hashes.size();
  if (num_total_hashes_to_resolve > 0) {
    LogIfError(
        sfe_metric_context->LogHistogram<metric::kKAnonTotalCacheHitPercentage>(
            (static_cast<double>(num_resolved_k_anon_hashes +
                                 num_resolved_non_k_anon_hashes)) /
            num_total_hashes_to_resolve * 100.0));
  }

  absl::flat_hash_set<absl::string_view> unresolved_hashes;
  for (const auto& hash : hashes) {
    if (!k_anon_hashes.contains(hash) && !non_k_anon_hashes.contains(hash)) {
      unresolved_hashes.insert(hash);
    }
  }
  if (unresolved_hashes.empty()) {
    PS_VLOG(5) << "No hashes to query from k-anon service, returning k-anon "
                  "hashes queried from k_anon_cache: "
               << KAnonHashSetsToString(k_anon_hashes);
    callback(absl::flat_hash_set<std::string>(k_anon_hashes.begin(),
                                              k_anon_hashes.end()));
    return absl::OkStatus();
  }

  auto log_hit_rate_then_callback =
      [sfe_metric_context, num_total_hashes_to_resolve,
       num_resolved_k_anon_hashes, num_resolved_non_k_anon_hashes,
       callback = std::move(callback)](
          absl::StatusOr<absl::flat_hash_set<std::string>> k_anon_hashes) {
        if (k_anon_hashes.ok()) {
          int num_k_anon_hashes = k_anon_hashes->size();
          if (num_k_anon_hashes > 0) {
            LogIfError(
                sfe_metric_context
                    ->LogHistogram<metric::kKAnonCacheHitPercentage>(
                        (static_cast<double>(num_resolved_k_anon_hashes)) /
                        num_k_anon_hashes * 100.0));
          }
          int num_non_k_anon_hashes =
              num_total_hashes_to_resolve - num_k_anon_hashes;
          if (num_non_k_anon_hashes > 0) {
            LogIfError(
                sfe_metric_context
                    ->LogHistogram<metric::kNonKAnonCacheHitPercentage>(
                        (static_cast<double>(num_resolved_non_k_anon_hashes)) /
                        num_non_k_anon_hashes * 100.0));
          }
        }
        callback(std::move(k_anon_hashes));
      };
  PS_RETURN_IF_ERROR(QueryKAnonClient(
      unresolved_hashes, set_type, std::move(log_hit_rate_then_callback),
      std::move(k_anon_hashes), sfe_metric_context));
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers
