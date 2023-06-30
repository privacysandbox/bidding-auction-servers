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

#ifndef SERVICES_COMMON_CONCURRENT_STATIC_LOCAL_CACHE_H_
#define SERVICES_COMMON_CONCURRENT_STATIC_LOCAL_CACHE_H_

#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "services/common/concurrent/local_cache.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class provides a local (in-memory), thread-safe cache for storing
// and accessing a hash map. It takes ownership of the underlying hash map and
// enforces a no update interface on the hash map. This ensures safe look-ups
// across threads without locks as the hash map is static after construction.
template <class Key, class Value>
class StaticLocalCache : public LocalCache<Key, std::shared_ptr<Value>> {
 public:
  explicit StaticLocalCache(
      std::unique_ptr<absl::flat_hash_map<Key, std::shared_ptr<Value>>>
          hash_map)
      : static_hash_map_(std::move(hash_map)) {}
  virtual ~StaticLocalCache() = default;

  // StaticLocalCache is neither copyable nor movable.
  StaticLocalCache(const StaticLocalCache&) = delete;
  StaticLocalCache& operator=(const StaticLocalCache&) = delete;

  // Looks up and returns a shared_ptr to the Value if it exists,
  // otherwise returns an empty shared_ptr.
  std::shared_ptr<Value> LookUp(Key key) override {
    if (auto it = static_hash_map_->find(key); it != static_hash_map_->end()) {
      return it->second;
    } else {
      return std::shared_ptr<Value>();
    }
  }

 private:
  std::unique_ptr<const absl::flat_hash_map<Key, std::shared_ptr<Value>>>
      static_hash_map_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CONCURRENT_STATIC_LOCAL_CACHE_H_
