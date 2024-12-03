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

#include "services/seller_frontend_service/k_anon/k_anon_cache.h"

#include <memory>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "grpc/event_engine/event_engine.h"
#include "include/gtest/gtest.h"
#include "services/common/test/utils/test_init.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr int kTestCacheCapacity = 2;
constexpr absl::Duration kTestCacheDefaultTimeoutDuration = absl::Minutes(30);
constexpr char kTestHash1[] = "hash1";
constexpr char kTestHash2[] = "hash2";
constexpr char kTestHash3[] = "hash3";

class KAnonCacheTest : public ::testing::Test {
 public:
  void SetUp() {
    CommonTestInit();
    server_common::log::SetGlobalPSVLogLevel(20);
    executor_ = std::make_unique<server_common::EventEngineExecutor>(
        grpc_event_engine::experimental::CreateEventEngine());
    cache_ = std::make_unique<KAnonCache>(
        kTestCacheCapacity, kTestCacheDefaultTimeoutDuration, executor_.get());
  }

 protected:
  std::unique_ptr<server_common::EventEngineExecutor> executor_;
  server_common::GrpcInit gprc_init;
  std::unique_ptr<KAnonCache> cache_;
};

TEST_F(KAnonCacheTest, CanAddElement) {
  CHECK_OK(cache_->Insert({kTestHash1}));
  EXPECT_TRUE(cache_->Query({kTestHash1}).contains(kTestHash1));
}

TEST_F(KAnonCacheTest, EvictsLeastRecentlyUsedElement) {
  CHECK_OK(cache_->Insert({kTestHash1}));
  CHECK_OK(cache_->Insert({kTestHash2}));

  // Querying the hash makes it most recently used.
  EXPECT_TRUE(cache_->Query({kTestHash1}).contains(kTestHash1));
  EXPECT_TRUE(cache_->Query({kTestHash2}).contains(kTestHash2));

  // Cache is now at capacity and adding a new hash should evict the LRU entry.
  CHECK_OK(cache_->Insert({kTestHash3}));
  EXPECT_FALSE(cache_->Query({kTestHash1}).contains(kTestHash1));
  EXPECT_TRUE(cache_->Query({kTestHash2}).contains(kTestHash2));
  EXPECT_TRUE(cache_->Query({kTestHash3}).contains(kTestHash3));
}

TEST_F(KAnonCacheTest, KeysGetDeduplicated) {
  CHECK_OK(cache_->Insert({kTestHash1}));
  CHECK_OK(cache_->Insert({kTestHash1}));
  auto cached_hashes = cache_->GetAllHashesForTesting();
  ASSERT_EQ(cached_hashes.size(), 1);
  EXPECT_TRUE(cached_hashes.contains(kTestHash1));
}

TEST_F(KAnonCacheTest, KeysExpire) {
  KAnonCache cache(kTestCacheCapacity, absl::Nanoseconds(1), executor_.get());
  CHECK_OK(cache.Insert({kTestHash1}));
  sleep(1);
  EXPECT_FALSE(cache.Query({kTestHash1}).contains(kTestHash1));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
