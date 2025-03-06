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

#include "services/seller_frontend_service/k_anon/k_anon_cache_manager.h"

#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/internal/endian.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "grpc/event_engine/event_engine.h"
#include "include/gtest/gtest.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "services/seller_frontend_service/cache/cache.h"
#include "services/seller_frontend_service/k_anon/k_anon_cache_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using ::google::chrome::kanonymityquery::v1::ValidateHashesRequest;
using ::google::chrome::kanonymityquery::v1::ValidateHashesResponse;

using KAnonClientCallBack = absl::AnyInvocable<
    void(absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>) &&>;
using ShardedCache = KAnonCacheManager::ShardedKAnonCache;

constexpr absl::string_view kTestHash1 =
    "af316ecb91a8ee7ae99210702b2d4758f30cdde3bf61e3d8e787d74681f90a6e";
constexpr absl::string_view kTestHash2 =
    "e7bf382f6e5915b3f88619b866223ebf1d51c4c5321cccde2e9ff700a3259086";
constexpr absl::string_view kTestHash3 =
    "42caa4abb7b60f8f914e5bfb8e6511d7d9bd9817de719b74251755d97fe97bf1";
constexpr absl::string_view kTestHash4 =
    "1c27099b3b84b13d0e3fbd299ba93ae7853ec1d0d3a4e5daa89e68b7ad59d7cb";
constexpr absl::string_view kTestHash5 =
    "0c000000112233445566778899aabbccddeeff00112233445566778899aabbcc";
constexpr absl::string_view kTestHash6 =
    "0c00000062bedb8b60ce05c1decfe3ad16b72230967de01f640b7e4729b49fce";

constexpr absl::string_view kSetType = "fledge";

class KAnonCacheManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    CommonTestInit();
    server_common::log::SetGlobalPSVLogLevel(20);

    executor_ = std::make_unique<server_common::EventEngineExecutor>(
        grpc_event_engine::experimental::CreateEventEngine());
    client_ = std::make_unique<MockKAnonClient>();
    server_common::telemetry::TelemetryConfig config_proto;
    config_proto.set_mode(server_common::telemetry::TelemetryConfig::PROD);
    metric::MetricContextMap<SelectAdRequest>(
        std::make_unique<server_common::telemetry::BuildDependentConfig>(
            config_proto));
    metric::SfeContextMap()->Get(&select_ad_request_);
    auto fetched_sfe_context =
        metric::SfeContextMap()->Remove(&select_ad_request_);
    CHECK_OK(fetched_sfe_context);
    sfe_context_ = *std::move(fetched_sfe_context);
  }

 protected:
  std::unique_ptr<server_common::EventEngineExecutor> executor_;
  server_common::GrpcInit gprc_init;
  std::unique_ptr<MockKAnonClient> client_;
  SelectAdRequest select_ad_request_;
  std::unique_ptr<metric::SfeContext> sfe_context_;
  KAnonCacheManagerConfig config_;
};

TEST_F(KAnonCacheManagerTest, CallsKAnonClient) {
  auto k_anon_cache = std::make_unique<MockCache<std::string, std::string>>();
  auto non_k_anon_cache =
      std::make_unique<MockCache<std::string, std::string>>();

  absl::flat_hash_set<absl::string_view> unresolved_hash_set = {
      kTestHash1, kTestHash2, kTestHash3, kTestHash4};

  EXPECT_CALL(*k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {{std::string(kTestHash1), std::string(kTestHash1)}};
          });
  EXPECT_CALL(*non_k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {};
          });

  EXPECT_CALL(*client_, Execute)
      .WillOnce([](std::unique_ptr<ValidateHashesRequest> request,
                   KAnonClientCallBack on_done, absl::Duration timeout) {
        absl::flat_hash_set<std::string> hashes_to_be_queried;
        for (auto& type_sets_map : request->sets()) {
          EXPECT_EQ(type_sets_map.type(), kSetType);
          for (const auto& hash : type_sets_map.hashes()) {
            hashes_to_be_queried.insert(hash);
          }
        }
        EXPECT_EQ(hashes_to_be_queried.size(), 3);
        EXPECT_FALSE(hashes_to_be_queried.contains(kTestHash1));

        auto response = std::make_unique<ValidateHashesResponse>();
        auto* response_type_set = response->add_k_anonymous_sets();
        response_type_set->set_type(kSetType);
        response_type_set->add_hashes(kTestHash2);
        response_type_set->add_hashes(kTestHash3);
        std::move(on_done)(std::move(response));
        return absl::OkStatus();
      });

  EXPECT_CALL(*k_anon_cache, Insert)
      .WillOnce(
          [](const absl::flat_hash_map<std::string, std::string>& hashes) {
            EXPECT_EQ(hashes.size(), 3);
            EXPECT_TRUE(hashes.contains(kTestHash1));
            EXPECT_TRUE(hashes.contains(kTestHash2));
            EXPECT_TRUE(hashes.contains(kTestHash3));
            return absl::OkStatus();
          });

  EXPECT_CALL(*non_k_anon_cache, Insert)
      .WillOnce(
          [](const absl::flat_hash_map<std::string, std::string>& hashes) {
            EXPECT_EQ(hashes.size(), 1);
            EXPECT_TRUE(hashes.contains(kTestHash4));
            return absl::OkStatus();
          });

  auto on_done_hash_query =
      [](absl::StatusOr<absl::flat_hash_set<std::string>> hashes) {
        EXPECT_EQ(hashes->size(), 3);
        EXPECT_FALSE(hashes->contains(kTestHash4));
        return;
      };

  ShardedCache sharded_k_anon_caches;
  ShardedCache sharded_non_k_anon_caches;
  sharded_k_anon_caches.emplace_back(std::move(k_anon_cache));
  sharded_non_k_anon_caches.emplace_back(std::move(non_k_anon_cache));

  KAnonCacheManager cache_manager(std::move(sharded_k_anon_caches),
                                  std::move(sharded_non_k_anon_caches),
                                  std::move(client_), config_);
  auto status = cache_manager.AreKAnonymous(
      kSetType, unresolved_hash_set, on_done_hash_query, sfe_context_.get());
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(KAnonCacheManagerTest, ReturnsHashInKAnonCache) {
  auto k_anon_cache = std::make_unique<MockCache<std::string, std::string>>();
  auto non_k_anon_cache =
      std::make_unique<MockCache<std::string, std::string>>();
  absl::flat_hash_set<absl::string_view> unresolved_hash_set = {
      kTestHash1, kTestHash2, kTestHash3};

  EXPECT_CALL(*k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {{std::string(kTestHash1), std::string(kTestHash1)},
                    {std::string(kTestHash2), std::string(kTestHash2)}};
          });
  EXPECT_CALL(*non_k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {{std::string(kTestHash3), std::string(kTestHash3)}};
          });

  EXPECT_CALL(*client_, Execute).Times(0);

  auto on_done_hash_query =
      [](absl::StatusOr<absl::flat_hash_set<std::string>> hashes) {
        EXPECT_EQ(hashes->size(), 2);
        EXPECT_TRUE(hashes->contains(kTestHash1));
        EXPECT_TRUE(hashes->contains(kTestHash2));
        EXPECT_FALSE(hashes->contains(kTestHash3));
      };

  ShardedCache sharded_k_anon_caches;
  ShardedCache sharded_non_k_anon_caches;
  sharded_k_anon_caches.emplace_back(std::move(k_anon_cache));
  sharded_non_k_anon_caches.emplace_back(std::move(non_k_anon_cache));

  KAnonCacheManager cache_manager(std::move(sharded_k_anon_caches),
                                  std::move(sharded_non_k_anon_caches),
                                  std::move(client_), config_);
  auto status = cache_manager.AreKAnonymous(
      kSetType, unresolved_hash_set, on_done_hash_query, sfe_context_.get());
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(KAnonCacheManagerTest, ReturnsEmptySetIfNoKAnonHashFound) {
  auto k_anon_cache = std::make_unique<MockCache<std::string, std::string>>();
  auto non_k_anon_cache =
      std::make_unique<MockCache<std::string, std::string>>();
  absl::flat_hash_set<absl::string_view> unresolved_hash_set = {kTestHash1};

  EXPECT_CALL(*k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {};
          });
  EXPECT_CALL(*non_k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {};
          });

  EXPECT_CALL(*client_, Execute)
      .WillOnce([](std::unique_ptr<ValidateHashesRequest> request,
                   KAnonClientCallBack on_done,
                   absl::Duration timeout) { return absl::OkStatus(); });

  auto on_done_hash_query =
      [](absl::StatusOr<absl::flat_hash_set<std::string>> hashes) {
        EXPECT_TRUE(hashes->empty());
      };

  ShardedCache sharded_k_anon_caches;
  ShardedCache sharded_non_k_anon_caches;
  sharded_k_anon_caches.emplace_back(std::move(k_anon_cache));
  sharded_non_k_anon_caches.emplace_back(std::move(non_k_anon_cache));

  KAnonCacheManager cache_manager(std::move(sharded_k_anon_caches),
                                  std::move(sharded_non_k_anon_caches),
                                  std::move(client_), config_);
  auto status = cache_manager.AreKAnonymous(
      kSetType, unresolved_hash_set, on_done_hash_query, sfe_context_.get());
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(KAnonCacheManagerTest, ReturnsErrorAndCacheHashIfClientErrors) {
  auto k_anon_cache = std::make_unique<MockCache<std::string, std::string>>();
  auto non_k_anon_cache =
      std::make_unique<MockCache<std::string, std::string>>();
  absl::flat_hash_set<absl::string_view> unresolved_hash_set = {kTestHash1,
                                                                kTestHash2};
  absl::string_view error_message = "error from k-anon client";

  EXPECT_CALL(*k_anon_cache, Query)
      .WillOnce(
          [&unresolved_hash_set](const absl::flat_hash_set<std::string>& hashes)
              -> absl::flat_hash_map<std::string, std::string> {
            for (auto& hash : unresolved_hash_set) {
              EXPECT_TRUE(hashes.contains(hash));
            }
            return {{std::string(kTestHash1), std::string(kTestHash1)}};
          });
  EXPECT_CALL(*non_k_anon_cache, Query);

  EXPECT_CALL(*client_, Execute)
      .WillOnce([&error_message](std::unique_ptr<ValidateHashesRequest> request,
                                 KAnonClientCallBack on_done,
                                 absl::Duration timeout) {
        return absl::InternalError(error_message);
      });

  auto on_done_hash_query =
      [](const absl::StatusOr<absl::flat_hash_set<std::string>>& hashes) {
        EXPECT_EQ(hashes->size(), 1);
        EXPECT_TRUE(hashes->contains(kTestHash1));
      };

  ShardedCache sharded_k_anon_caches;
  ShardedCache sharded_non_k_anon_caches;
  sharded_k_anon_caches.emplace_back(std::move(k_anon_cache));
  sharded_non_k_anon_caches.emplace_back(std::move(non_k_anon_cache));

  KAnonCacheManager cache_manager(std::move(sharded_k_anon_caches),
                                  std::move(sharded_non_k_anon_caches),
                                  std::move(client_), config_);
  auto status = cache_manager.AreKAnonymous(
      kSetType, unresolved_hash_set, on_done_hash_query, sfe_context_.get());
  EXPECT_EQ(status, absl::InternalError(error_message));
}

TEST_F(KAnonCacheManagerTest, CallsOnACorrectShardedCache) {
  int num_k_anon_shard = 3;
  int num_non_k_anon_shard = 2;

  std::vector<std::unique_ptr<MockCache<std::string, std::string>>>
      mock_k_anon_caches;
  std::vector<std::unique_ptr<MockCache<std::string, std::string>>>
      mock_non_k_anon_caches;
  mock_k_anon_caches.reserve(num_k_anon_shard);
  mock_non_k_anon_caches.reserve(num_non_k_anon_shard);
  for (int i = 0; i < num_k_anon_shard; i++) {
    mock_k_anon_caches.emplace_back(
        std::make_unique<MockCache<std::string, std::string>>());
  }
  for (int i = 0; i < num_non_k_anon_shard; i++) {
    mock_non_k_anon_caches.emplace_back(
        std::make_unique<MockCache<std::string, std::string>>());
  }

  // Expected sharding split for k-anon:
  //  cache at index 0: kTestHash5, kTestHash6
  //  cache at index 1: kTestHash3, kTestHash4
  //  cache at index 2: kTestHash1, kTestHash2
  // Expected sharding split for non k-anon:
  //  cache at index 0: kTestHash3, kTestHash5, kTestHash6
  //  cache at index 1: kTestHash1, kTestHash2, kTestHash4
  absl::flat_hash_set<absl::string_view> unresolved_hash_set = {
      kTestHash1, kTestHash2, kTestHash3, kTestHash4, kTestHash5, kTestHash6};

  // No hash will be resolved by the caches.
  for (int i = 0; i < num_k_anon_shard; i++) {
    EXPECT_CALL(*mock_k_anon_caches[i], Query)
        .WillOnce([&unresolved_hash_set, &num_k_anon_shard, cache_index = i](
                      const absl::flat_hash_set<std::string>& hashes)
                      -> absl::flat_hash_map<std::string, std::string> {
          for (auto& hash : unresolved_hash_set) {
            if (absl::little_endian::Load32(hash.data()) % num_k_anon_shard ==
                cache_index) {
              EXPECT_TRUE(hashes.contains(hash));
            }
          }
          return {};
        });
  }

  for (int i = 0; i < num_non_k_anon_shard; i++) {
    EXPECT_CALL(*mock_non_k_anon_caches[i], Query)
        .WillOnce(
            [&unresolved_hash_set, &num_non_k_anon_shard,
             cache_index = i](const absl::flat_hash_set<std::string>& hashes)
                -> absl::flat_hash_map<std::string, std::string> {
              for (auto& hash : unresolved_hash_set) {
                if (absl::little_endian::Load32(hash.data()) %
                        num_non_k_anon_shard ==
                    cache_index) {
                  EXPECT_TRUE(hashes.contains(hash));
                }
              }
              return {};
            });
  }

  // kTestHash1, kTestHash3, kTestHash5 will be resolved as k-anon by client.
  EXPECT_CALL(*client_, Execute)
      .WillOnce([](std::unique_ptr<ValidateHashesRequest> request,
                   KAnonClientCallBack on_done, absl::Duration timeout) {
        absl::flat_hash_set<absl::string_view> hashes_to_be_queried;
        for (auto& type_sets_map : request->sets()) {
          EXPECT_EQ(type_sets_map.type(), kSetType);
          for (const auto& hash : type_sets_map.hashes()) {
            hashes_to_be_queried.insert(hash);
          }
        }
        EXPECT_EQ(hashes_to_be_queried.size(), 6);

        auto response = std::make_unique<ValidateHashesResponse>();
        auto* response_type_set = response->add_k_anonymous_sets();
        response_type_set->set_type(kSetType);
        response_type_set->add_hashes(kTestHash1);
        response_type_set->add_hashes(kTestHash3);
        response_type_set->add_hashes(kTestHash5);
        std::move(on_done)(std::move(response));
        return absl::OkStatus();
      });

  // Expected k-anon hashes to be Inserted after client query:
  //  cache at index 0: kTestHash5
  //  cache at index 1: kTestHash3
  //  cache at index 2: kTestHash1
  for (int i = 0; i < num_k_anon_shard; i++) {
    EXPECT_CALL(*mock_k_anon_caches[i], Insert)
        .WillOnce(
            [&num_k_anon_shard, cache_index = i](
                const absl::flat_hash_map<std::string, std::string>& hashes) {
              for (auto& hash : MapToSet(hashes)) {
                EXPECT_EQ(
                    absl::little_endian::Load32(hash.data()) % num_k_anon_shard,
                    cache_index);
              }
              EXPECT_EQ(hashes.size(), 1);
              switch (cache_index) {
                case 0:
                  EXPECT_TRUE(hashes.contains(kTestHash5));
                  break;
                case 1:
                  EXPECT_TRUE(hashes.contains(kTestHash3));
                  break;
                case 2:
                  EXPECT_TRUE(hashes.contains(kTestHash1));
                  break;
                default:
                  return absl::InvalidArgumentError("Unexpected cache index.");
              }
              return absl::OkStatus();
            });
  }

  // Expected non k-anon hashes to be Inserted after client query:
  //  cache at index 0: kTestHash6
  //  cache at index 1: kTestHash2, kTestHash4
  for (int i = 0; i < num_non_k_anon_shard; i++) {
    EXPECT_CALL(*mock_non_k_anon_caches[i], Insert)
        .WillOnce(
            [&num_non_k_anon_shard, cache_index = i](
                const absl::flat_hash_map<std::string, std::string>& hashes) {
              for (auto& hash : MapToSet(hashes)) {
                EXPECT_EQ(absl::little_endian::Load32(hash.data()) %
                              num_non_k_anon_shard,
                          cache_index);
              }
              switch (cache_index) {
                case 0:
                  EXPECT_EQ(hashes.size(), 1);
                  EXPECT_TRUE(hashes.contains(kTestHash6));
                  break;
                case 1:
                  EXPECT_EQ(hashes.size(), 2);
                  EXPECT_TRUE(hashes.contains(kTestHash2));
                  EXPECT_TRUE(hashes.contains(kTestHash4));
                  break;
                default:
                  return absl::InvalidArgumentError("Unexpected cache index.");
              }
              return absl::OkStatus();
            });
  }

  auto on_done_hash_query =
      [](absl::StatusOr<absl::flat_hash_set<std::string>> hashes) {
        EXPECT_EQ(hashes->size(), 3);
        EXPECT_TRUE(hashes->contains(kTestHash1));
        EXPECT_TRUE(hashes->contains(kTestHash3));
        EXPECT_TRUE(hashes->contains(kTestHash5));
        return;
      };

  ShardedCache sharded_k_anon_caches;
  ShardedCache sharded_non_k_anon_caches;
  sharded_k_anon_caches.reserve(num_k_anon_shard);
  sharded_non_k_anon_caches.reserve(num_non_k_anon_shard);
  for (int i = 0; i < num_k_anon_shard; i++) {
    sharded_k_anon_caches.emplace_back(std::move(mock_k_anon_caches[i]));
  }
  for (int i = 0; i < num_non_k_anon_shard; i++) {
    sharded_non_k_anon_caches.emplace_back(
        std::move(mock_non_k_anon_caches[i]));
  }

  config_.num_k_anon_shards = num_k_anon_shard;
  config_.num_non_k_anon_shards = num_non_k_anon_shard;
  KAnonCacheManager cache_manager(std::move(sharded_k_anon_caches),
                                  std::move(sharded_non_k_anon_caches),
                                  std::move(client_), config_);
  auto status = cache_manager.AreKAnonymous(
      kSetType, unresolved_hash_set, on_done_hash_query, sfe_context_.get());
  ASSERT_TRUE(status.ok()) << status;
}

TEST_F(KAnonCacheManagerTest, DoesNotCallCacheWhenDisabled) {
  auto k_anon_cache = std::make_unique<MockCache<std::string, std::string>>();
  auto non_k_anon_cache =
      std::make_unique<MockCache<std::string, std::string>>();

  absl::flat_hash_set<absl::string_view> unresolved_hash_set = {kTestHash1,
                                                                kTestHash2};

  EXPECT_CALL(*k_anon_cache, Query).Times(0);
  EXPECT_CALL(*non_k_anon_cache, Query).Times(0);

  EXPECT_CALL(*client_, Execute)
      .WillOnce([](std::unique_ptr<ValidateHashesRequest> request,
                   KAnonClientCallBack on_done, absl::Duration timeout) {
        absl::flat_hash_set<std::string> hashes_to_be_queried;
        for (auto& type_sets_map : request->sets()) {
          EXPECT_EQ(type_sets_map.type(), kSetType);
          for (const auto& hash : type_sets_map.hashes()) {
            hashes_to_be_queried.insert(hash);
          }
        }
        EXPECT_EQ(hashes_to_be_queried.size(), 2);

        auto response = std::make_unique<ValidateHashesResponse>();
        auto* response_type_set = response->add_k_anonymous_sets();
        response_type_set->set_type(kSetType);
        response_type_set->add_hashes(kTestHash1);
        std::move(on_done)(std::move(response));
        return absl::OkStatus();
      });

  EXPECT_CALL(*k_anon_cache, Insert).Times(0);
  EXPECT_CALL(*non_k_anon_cache, Insert).Times(0);

  auto on_done_hash_query =
      [](absl::StatusOr<absl::flat_hash_set<std::string>> hashes) {
        EXPECT_EQ(hashes->size(), 1);
        EXPECT_TRUE(hashes->contains(kTestHash1));
        EXPECT_FALSE(hashes->contains(kTestHash2));
        return;
      };

  ShardedCache sharded_k_anon_caches;
  ShardedCache sharded_non_k_anon_caches;
  sharded_k_anon_caches.emplace_back(std::move(k_anon_cache));
  sharded_non_k_anon_caches.emplace_back(std::move(non_k_anon_cache));

  config_.enable_k_anon_cache = false;
  KAnonCacheManager cache_manager(std::move(sharded_k_anon_caches),
                                  std::move(sharded_non_k_anon_caches),
                                  std::move(client_), config_);
  auto status = cache_manager.AreKAnonymous(
      kSetType, unresolved_hash_set, on_done_hash_query, sfe_context_.get());
  EXPECT_TRUE(status.ok()) << status;
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
