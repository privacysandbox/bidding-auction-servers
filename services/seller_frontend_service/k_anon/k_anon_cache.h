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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_H_

#include <string>

#include <event2/event.h>
#include <event2/event_struct.h>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/common/util/event.h"
#include "services/common/util/event_base.h"
#include "services/seller_frontend_service/k_anon/doubly_linked_list.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {

class KAnonCache {
 public:
  // Constructs a cache with provided `capacity` which determines how
  // many entries can be stored in the cache. Uses `ttl` to initialize
  // the TTL of each hash newly inserted into cache.
  explicit KAnonCache(int capacity, absl::Duration ttl,
                      server_common::Executor* executor);
  ~KAnonCache();

  // Answers whether or not a certain hash in the input is present in the
  // cache. Adjusts the cache to mark the queried hashes as most recently
  // accessed.
  absl::flat_hash_set<std::string> Query(
      const absl::flat_hash_set<std::string>& hashes)
      ABSL_LOCKS_EXCLUDED(cache_mutex_);

  // Inserts the hashes into cache. If there isn't enough space, it evicts the
  // required number of entries to make space. If the number of entries being
  // inserted exceed the cache size then only the entries that fall in capacity
  // are inserted.
  //
  // Returns the status of insertion operation.
  absl::Status Insert(const absl::flat_hash_set<std::string>& hashes)
      ABSL_LOCKS_EXCLUDED(cache_mutex_);

  // Returns all hashes (used for testing only).
  absl::flat_hash_set<std::string> GetAllHashesForTesting()
      ABSL_LOCKS_EXCLUDED(cache_mutex_);

 protected:
  // Callback to invoke upon an entry expiration.
  static void EntryExpiryCallback(int fd, short what, void* arg);

  // Notifies about the start of event loop. Helps ensure that the shutdown does
  // not happen before the eventloop is started.
  static void StartedEventLoop(int fd, short event_type, void* arg);

  // Shuts down the event loop and signals the notification about shutdown.
  static void ShutdownEventLoop(int fd, short event_type, void* arg);

  // Evicts (at most) `N` entries.
  absl::Status Evict(int N) ABSL_EXCLUSIVE_LOCKS_REQUIRED(cache_mutex_);

 private:
  // Mutex to provide safe access to concurrent threads.
  absl::Mutex cache_mutex_;

  // Structures to help find entries in the cache and keep them sorted based
  // on recency.
  DoublyLinkedList dll_ ABSL_GUARDED_BY(cache_mutex_);
  absl::flat_hash_map<std::string, Node*> hash_to_node_
      ABSL_GUARDED_BY(cache_mutex_);

  int num_entries_;
  int capacity_;
  struct timeval expiry_;

  server_common::Executor* executor_;

  // Synchronizes the status of shutdown for destructor and execution loop.
  absl::Notification eventloop_started_;
  absl::Notification shutdown_requested_;
  absl::Notification shutdown_complete_;

  // Used for dispatching an event loop.
  EventBase event_base_;

  // Triggers on event loop start.
  Event eventloop_started_event_;

  // Used to detect object destruction and to shut down the event loop.
  Event shutdown_timer_event_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_K_ANON_K_ANON_CACHE_H_
