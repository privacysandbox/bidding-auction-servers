// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/concurrent/static_local_cache.h"

#include <future>
#include <string>

#include "gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

class MockClass {};

TEST(StaticLocalCacheTest, LookUpReturnsNullPtrIfKeyNotFound) {
  auto input_hash_map = std::make_unique<
      absl::flat_hash_map<std::string, std::shared_ptr<MockClass>>>();
  StaticLocalCache<std::string, MockClass> class_under_test(
      std::move(input_hash_map));

  std::shared_ptr<MockClass> output =
      class_under_test.LookUp(MakeARandomString());

  EXPECT_EQ(output, nullptr);
}

TEST(StaticLocalCacheTest, LookUpReturnsCachedValue) {
  std::string key = MakeARandomString();
  auto expected_output = std::make_shared<MockClass>();
  auto input_hash_map = std::make_unique<
      absl::flat_hash_map<std::string, std::shared_ptr<MockClass>>>();
  input_hash_map->emplace(key, expected_output);
  StaticLocalCache<std::string, MockClass> class_under_test(
      std::move(input_hash_map));

  std::shared_ptr<MockClass> output = class_under_test.LookUp(key);

  EXPECT_EQ(output, expected_output);
}

TEST(StaticLocalCacheTest, LookUpReturnsCachedValueAcrossThreads) {
  std::string key = MakeARandomString();
  auto expected_output = std::make_shared<MockClass>();
  auto input_hash_map = std::make_unique<
      absl::flat_hash_map<std::string, std::shared_ptr<MockClass>>>();
  input_hash_map->emplace(key, expected_output);
  StaticLocalCache<std::string, MockClass> class_under_test(
      std::move(input_hash_map));

  std::shared_ptr<MockClass> output = class_under_test.LookUp(key);

  auto get_val = [&key, &class_under_test] {
    return class_under_test.LookUp(key);
  };

  auto future_1 = std::async(std::launch::async, get_val);
  auto future_2 = std::async(std::launch::async, get_val);

  EXPECT_EQ(future_1.get(), future_2.get());
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
