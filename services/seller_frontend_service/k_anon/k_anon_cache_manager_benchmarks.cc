/*
 * Copyright 2025 Google LLC
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

// Run the benchmark as follows:
// builders/tools/bazel-debian run --dynamic_mode=off -c opt --copt=-gmlt \
//   --copt=-fno-omit-frame-pointer --fission=yes --strip=never \
//   services/seller_frontend_service/k_anon:k_anon_cache_manager_benchmarks \
//   -- --benchmark_time_unit=us --benchmark_repetitions=10

#include <random>

#include "absl/algorithm/container.h"
#include "absl/container/btree_set.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "services/common/cache/cache.h"
#include "services/common/clients/k_anon_server/k_anon_client.h"
#include "services/common/clients/k_anon_server/k_anon_client_mock.h"
#include "services/common/test/utils/test_init.h"
#include "services/common/util/hash_util.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using KAnonClientCallBack = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>) &&>;

constexpr int kRangeArg = 0;
// There are 4 outcomes for unresolved hashes: resolved as k-anon by cache
// query, resolved as non-k-anon query, resolved as k-anon by client query, or
// unresolved after all the queries (which get inserted into non-k-anon cache).
constexpr int kHashOutcomeSplit = 4;
constexpr int kNumKAnonCacheShards = 3;
constexpr int kNumNonKAnonCacheShards = 3;
constexpr int kNumKAnonymousCalls = 3;
constexpr absl::string_view kFledgeSetType = "fledge";

// Helper function that constructs unresolved hashes for k-anon query.
// Unresolved hasehes will include hashes returned by
// ConstructKAnonClientHash(), ConstructKAnonCacheHash(), and
// ConstructNonKAnonCacheHash() if range input is the same value.
absl::flat_hash_set<std::string> ConstructUnresolvedHash(int range) {
  // The range argument specifies the number of hashes to generate.
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
  // Creates hashes for 1 in 4 numbers in the given range.
  if (range < 2) {
    return {};
  }
  absl::flat_hash_set<std::string> hash_set;
  for (int i = 1; i < range; i += kHashOutcomeSplit) {
    hash_set.insert(ComputeSHA256(absl::StrCat(i), /* return_hex= */ false));
  }
  return hash_set;
}

absl::flat_hash_map<std::string, std::string> ConstructKAnonCacheHash(
    int range) {
  // Creates hashes for 1 in 4 numbers in the given range.
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
  // Creates hashes for 1 in 4 numbers in the given range.
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

// Make multiple calls to AreKAnonymous() to a KAnonCacheManager with 3 sharded
// caches.
static void BM_KAnonCacheManagerAreKAnonymous(benchmark::State& state) {
  // Setup.
  CommonTestInit();
  server_common::GrpcInit gprc_init;

  // Prepare unresolved hashes mock k-anon hashes.
  absl::flat_hash_set<std::string> unresolved =
      ConstructUnresolvedHash(state.range(kRangeArg));
  absl::flat_hash_set<std::string> resolved_by_client =
      ConstructKAnonClientHash(state.range(kRangeArg));

  // Create a mock k-anon client with some resolved hashes stored inside.
  std::unique_ptr<MockKAnonClient> mock_client =
      std::make_unique<MockKAnonClient>();
  EXPECT_CALL(*mock_client, Execute)
      .WillRepeatedly([resolved_by_client](
                          std::unique_ptr<ValidateHashesRequest> request,
                          KAnonClientCallBack on_done, absl::Duration timeout) {
        auto response = std::make_unique<ValidateHashesResponse>();
        auto* response_type_set = response->add_k_anonymous_sets();
        for (auto& type_sets_map : request->sets()) {
          response_type_set->set_type(type_sets_map.type());
          for (const auto& hash : type_sets_map.hashes()) {
            if (resolved_by_client.find(hash) != resolved_by_client.end()) {
              response_type_set->add_hashes(hash);
            }
          }
        }
        std::move(on_done)(std::move(response));
        return absl::OkStatus();
      });

  auto executor = std::make_unique<server_common::EventEngineExecutor>(
      grpc_event_engine::experimental::CreateEventEngine());

  KAnonCacheManagerConfig config = {
      .total_num_hash = static_cast<int>(state.range(kRangeArg)),
      .expected_k_anon_to_non_k_anon_ratio = 0.5,
      .num_k_anon_shards = kNumKAnonCacheShards,
      .num_non_k_anon_shards = kNumNonKAnonCacheShards};

  auto on_done =
      [](const absl::StatusOr<absl::flat_hash_set<std::string>>& response) {
        if (!response.ok()) {
          PS_VLOG(5) << "k-anon cache manager returned an error: "
                     << response.status();
        } else {
          PS_VLOG(5) << "k-anon cache manager returned a successful response";
        }
      };

  SelectAdRequest select_ad_request;
  server_common::telemetry::TelemetryConfig config_proto;
  config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
  metric::MetricContextMap<SelectAdRequest>(
      std::make_unique<server_common::telemetry::BuildDependentConfig>(
          config_proto));
  metric::SfeContextMap()->Get(&select_ad_request);
  auto fetched_sfe_context =
      metric::SfeContextMap()->Remove(&select_ad_request);

  CHECK_OK(fetched_sfe_context);
  std::unique_ptr<metric::SfeContext> sfe_context =
      *std::move(fetched_sfe_context);
  std::unique_ptr<KAnonCacheManager> k_anon_cache_manager =
      std::make_unique<KAnonCacheManager>(executor.get(),
                                          std::move(mock_client), config);

  absl::flat_hash_set<absl::string_view> unresolved_hashes =
      absl::flat_hash_set<absl::string_view>(unresolved.begin(),
                                             unresolved.end());
  for (auto _ : state) {
    // This code gets timed.
    for (int i = 0; i < kNumKAnonymousCalls; i++) {
      absl::Status status = k_anon_cache_manager->AreKAnonymous(
          kFledgeSetType, unresolved_hashes, on_done, sfe_context.get());
      if (!status.ok()) {
        PS_VLOG(5) << "Failed to query k-anon cache manager: " << status;
      }
    }
  }
}

// Register the function as a benchmark
BENCHMARK(BM_KAnonCacheManagerAreKAnonymous)->Range(8, 8 << 10);

// Run the benchmark
BENCHMARK_MAIN();

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
