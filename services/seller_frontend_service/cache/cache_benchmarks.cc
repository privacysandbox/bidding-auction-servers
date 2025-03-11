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

// Benchmark of cache.h in the context of a K-Anon cache.
// Run the benchmark as follows:
// builders/tools/bazel-debian run --dynamic_mode=off -c opt --copt=-gmlt \
//   --copt=-fno-omit-frame-pointer --fission=yes --strip=never \
//   services/seller_frontend_service/cache:cache_benchmarks -- \
//   --benchmark_time_unit=us --benchmark_repetitions=10

#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/hash_util.h"
#include "services/seller_frontend_service/cache/cache.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kRangeArg = 0;
// There are 4 outcomes for unresolved hashes: resolved as k-anon by cache
// query, resolved as non-k-anon query, resolved as k-anon by client query, or
// unresolved after all the queries (which get inserted into non-k-anon cache).
constexpr int kHashOutcomeSplit = 4;

absl::btree_set<std::string> MapToBTreeSet(
    absl::flat_hash_map<std::string, std::string> map) {
  absl::btree_set<std::string> set;
  std::transform(map.begin(), map.end(), std::inserter(set, set.end()),
                 [](const auto& entry) { return entry.first; });
  return set;
}

absl::flat_hash_set<std::string> ConstructUnresolvedHash(int range) {
  if (range < 1) {
    return {};
  }

  absl::flat_hash_set<std::string> hash_set;
  for (int i = 0; i < range; i++) {
    hash_set.insert(ComputeSHA256(absl::StrCat(i), /* return_hex= */ false));
  }

  return hash_set;
}

absl::flat_hash_set<std::string> ConstructKAnonClientHash(int range) {
  if (range < 2) {
    return {};
  }

  absl::flat_hash_set<std::string> hash_set;
  for (int i = 1; i < range; i += kHashOutcomeSplit) {
    hash_set.insert(ComputeSHA256(absl::StrCat(i), /* return_hex= */ false));
  }

  return hash_set;
}

absl::flat_hash_map<std::string, std::string> ConstructCacheHash(int range) {
  if (range < 3) {
    return {};
  }

  absl::flat_hash_map<std::string, std::string> map;
  for (int i = 2; i < range; i += kHashOutcomeSplit) {
    std::string str = ComputeSHA256(absl::StrCat(i), /* return_hex= */ false);
    map.insert({str, std::string()});
  }

  return map;
}

absl::flat_hash_map<std::string, std::string> ConstructNonKAnonCacheHash(
    int range) {
  if (range < 4) {
    return {};
  }

  absl::flat_hash_map<std::string, std::string> map;
  for (int i = 2; i < range; i += kHashOutcomeSplit) {
    std::string str = ComputeSHA256(absl::StrCat(i), /* return_hex= */ false);
    map.insert({str, std::string()});
  }

  return map;
}

// Query k-anon cache & non k-anon cache by passing the same set of unresolved
// hashes, then update non k-anon cache using a loop.
static void BM_QueryBothCacheWithAllHashes(benchmark::State& state) {
  // Setup.
  CommonTestInit();
  server_common::GrpcInit gprc_init;
  absl::flat_hash_set<std::string> unresolved =
      ConstructUnresolvedHash(state.range(kRangeArg));
  absl::flat_hash_set<std::string> resolved_by_client =
      ConstructKAnonClientHash(state.range(kRangeArg));
  std::unique_ptr<server_common::EventEngineExecutor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());
  Cache<std::string, std::string> k_anon_cache(
      unresolved.size(), absl::Minutes(30), executor.get());
  Cache<std::string, std::string> non_k_anon_cache(
      unresolved.size(), absl::Minutes(30), executor.get());
  auto status = k_anon_cache.Insert(ConstructCacheHash(state.range(kRangeArg)));
  status = non_k_anon_cache.Insert(
      ConstructNonKAnonCacheHash(state.range(kRangeArg)));

  for (auto _ : state) {
    // This code gets timed.
    absl::flat_hash_set<std::string> unresolved_by_caches;
    auto resolved_by_k_anon_cache = k_anon_cache.Query(unresolved);
    auto resolved_by_non_k_anon_cache = non_k_anon_cache.Query(unresolved);

    for (const auto& hash : unresolved) {
      if (!resolved_by_k_anon_cache.contains(hash) &&
          !resolved_by_non_k_anon_cache.contains(hash)) {
        unresolved_by_caches.insert(hash);
      }
    }

    absl::flat_hash_map<std::string, std::string> resolved;
    for (auto& hash : resolved_by_k_anon_cache) {
      resolved.insert(hash);
    }
    for (auto& hash : resolved_by_client) {
      resolved.insert({hash, std::string()});
    }
    resolved.merge(resolved_by_k_anon_cache);
    auto insert_status = k_anon_cache.Insert(resolved);

    absl::flat_hash_map<std::string, std::string> non_k_anon_hash;
    for (auto& hash : unresolved) {
      if (!resolved.contains(hash)) {
        non_k_anon_hash.insert({hash, hash});
      }
    }
    insert_status = non_k_anon_cache.Insert(non_k_anon_hash);
  }
}

// Use c_set_difference() to remove the hashes found in k-anon cache and query
// non k-anon cache with only the unresolved hashes.
static void BM_QueryCachesUsingSetDifference(benchmark::State& state) {
  // Setup.
  CommonTestInit();
  server_common::GrpcInit gprc_init;
  absl::flat_hash_set<std::string> unresolved =
      ConstructUnresolvedHash(state.range(kRangeArg));
  absl::flat_hash_set<std::string> resolved_by_client =
      ConstructKAnonClientHash(state.range(kRangeArg));
  std::unique_ptr<server_common::EventEngineExecutor> executor =
      std::make_unique<server_common::EventEngineExecutor>(
          grpc_event_engine::experimental::CreateEventEngine());
  Cache<std::string, std::string> k_anon_cache(
      unresolved.size(), absl::Minutes(30), executor.get());
  Cache<std::string, std::string> non_k_anon_cache(
      unresolved.size(), absl::Minutes(30), executor.get());
  auto status = k_anon_cache.Insert(ConstructCacheHash(state.range(kRangeArg)));
  status = non_k_anon_cache.Insert(
      ConstructNonKAnonCacheHash(state.range(kRangeArg)));

  for (auto _ : state) {
    // This code gets timed.
    auto resolved_by_k_anon_cache = k_anon_cache.Query(unresolved);
    absl::flat_hash_set<std::string> unresolved_by_k_anon_cache;
    absl::c_set_difference(
        MapToBTreeSet(resolved_by_k_anon_cache),
        absl::btree_set<std::string>(unresolved.begin(), unresolved.end()),
        std::inserter(unresolved_by_k_anon_cache,
                      unresolved_by_k_anon_cache.begin()));

    auto resolved_by_non_k_anon_cache =
        non_k_anon_cache.Query(unresolved_by_k_anon_cache);
    absl::flat_hash_set<std::string> unresolved_by_caches;
    absl::c_set_difference(
        MapToBTreeSet(resolved_by_non_k_anon_cache),
        absl::btree_set<std::string>(unresolved.begin(), unresolved.end()),
        std::inserter(unresolved_by_caches, unresolved_by_caches.begin()));

    absl::flat_hash_set<std::string> resolved;
    for (auto& hash : resolved_by_k_anon_cache) {
      resolved.insert(hash.first);
    }
    for (auto& hash : resolved_by_client) {
      resolved.insert(hash);
    }
    absl::Status insert_status;
    for (auto& hash : resolved) {
      insert_status = k_anon_cache.Insert({{hash, std::string()}});
    }

    absl::flat_hash_set<std::string> non_k_anon_hash;
    absl::c_set_difference(
        absl::btree_set<std::string>(resolved.begin(), resolved.end()),
        absl::btree_set<std::string>(unresolved.begin(), unresolved.end()),
        std::inserter(non_k_anon_hash, non_k_anon_hash.begin()));
    for (auto& hash : non_k_anon_hash) {
      insert_status = non_k_anon_cache.Insert({{hash, std::string()}});
    }
  }
}

// Register the function as a benchmark
BENCHMARK(BM_QueryBothCacheWithAllHashes)->Range(8, 8 << 10);
BENCHMARK(BM_QueryCachesUsingSetDifference)->Range(8, 8 << 10);

// Run the benchmark
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
