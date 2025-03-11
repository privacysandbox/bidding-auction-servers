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

#ifndef SERVICES_SELLER_FRONTEND_SERVICE_CACHE_CACHE_H_
#define SERVICES_SELLER_FRONTEND_SERVICE_CACHE_CACHE_H_

#include <memory>
#include <string>
#include <utility>

#include <event2/event.h>
#include <event2/event_struct.h>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "services/common/util/event.h"
#include "services/common/util/event_base.h"
#include "services/seller_frontend_service/cache/doubly_linked_list.h"
#include "services/seller_frontend_service/k_anon/k_anon_utils.h"
#include "src/concurrent/event_engine_executor.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename KeyT, typename ValueT>
class CacheInterface {
 public:
  CacheInterface() = default;
  virtual ~CacheInterface() = default;

  virtual absl::flat_hash_map<KeyT, ValueT> Query(
      const absl::flat_hash_set<KeyT>& keys) = 0;

  virtual absl::Status Insert(
      const absl::flat_hash_map<KeyT, ValueT>& entries) = 0;
};

template <typename KeyT, typename ValueT>
class Cache : public CacheInterface<KeyT, ValueT> {
 public:
  // Constructs a cache with provided `capacity` which determines how
  // many entries can be stored in the cache. Uses `ttl` to initialize
  // the TTL of each newly inserted entry into the cache.
  explicit Cache(
      int capacity, absl::Duration ttl, server_common::Executor* executor,
      absl::AnyInvocable<std::string(const KeyT&, const ValueT&)>
          entry_stringify_func =
              absl::AnyInvocable<std::string(const KeyT&, const ValueT&)>{
                  [](const KeyT& key, const ValueT& value) {
                    return absl::StrCat("[key: ", key, ", value: ", value, "]");
                  }})
      : num_entries_(0),
        capacity_(capacity),
        expiry_(absl::ToTimeval(ttl)),
        executor_(executor),
        entry_stringify_func_(std::move(entry_stringify_func)),
        eventloop_started_event_(Event(event_base_.get(), /* fd= */ -1,
                                       /* event_type= */ 0,
                                       /* event_callback= */ StartedEventLoop,
                                       /* arg= */ this,
                                       /* priority= */ 0, &OneMicrosecond)),
        shutdown_timer_event_(Event(event_base_.get(), /* fd= */ -1,
                                    /* event_type= */ EV_PERSIST,
                                    /* event_callback= */ ShutdownEventLoop,
                                    /* arg= */ this,
                                    /* priority= */ 0, &OneSecond)) {
    PS_VLOG(5) << "Creating cache with max capacity of: " << capacity;
    CHECK(executor_ != nullptr);
    executor_->Run([this]() {
      PS_VLOG(5) << "Scheduling event loop to handle expired entries in the "
                 << "cache";
      event_base_dispatch(event_base_.get());
    });
    PS_VLOG(5) << "Waiting for event loop to start";
    eventloop_started_.WaitForNotification();
    PS_VLOG(5) << "Event loop started";
  }

  ~Cache() {
    PS_VLOG(6)
        << "Ensuring that event loop has started before trying to stop it";
    eventloop_started_.WaitForNotification();
    shutdown_requested_.Notify();
    PS_VLOG(6) << "Will wait for event loop to stop";
    shutdown_complete_.WaitForNotification();
    PS_VLOG(6) << "Event loop shutdown signalled, will clean up the doubly "
               << "linked list (including references to events and event loop)";

    hash_to_node_.clear();
    while (dll_.Tail()) {
      dll_.Remove(dll_.Tail());
    }
  }

  // Answers whether or not a key in the input is present in the cache.
  // Adjusts the cache to mark the queried entries as most recently accessed.
  absl::flat_hash_map<KeyT, ValueT> Query(const absl::flat_hash_set<KeyT>& keys)
      ABSL_LOCKS_EXCLUDED(cache_mutex_) {
    PS_VLOG(5) << " " << __func__ << ": " << StringifyCacheKeys(keys);
    // For each key:
    //  Lookup the key in map:
    //    If found in map (then atomically):
    //    - Swap the node in the DLL with the front node.
    //    - Put the found entry into the set to return (can be done without
    //    lock).
    absl::flat_hash_map<KeyT, ValueT> found_entries;
    absl::MutexLock lock(&cache_mutex_);
    for (const auto& key : keys) {
      auto it = hash_to_node_.find(key);
      if (it == hash_to_node_.end()) {
        continue;
      }

      dll_.MoveToFront(it->second);
      found_entries.insert({key, it->second->data->value});
    }

    PS_VLOG(5) << " " << __func__
               << " found entries: " << StringifyCacheEntries(found_entries);
    return found_entries;
  }

  // Inserts the entries into cache. If there isn't enough space, it evicts the
  // required number of entries to make space. If the number of entries being
  // inserted exceed the cache size, then only the entries that fall in capacity
  // are inserted.
  //
  // Returns the status of insertion operation.
  absl::Status Insert(const absl::flat_hash_map<KeyT, ValueT>& entries)
      ABSL_LOCKS_EXCLUDED(cache_mutex_) {
    PS_VLOG(5) << " " << __func__ << ": " << StringifyCacheEntries(entries);
    // For each entry:
    //  - Check if cache is under capacity.
    //  - Evict a LRU entry if over capacity.
    //  - Update DLL and map with new node (Need writer mutex lock).
    //  - Insert an ephemeral timer event in event loop (with callack set to
    //      delete that entry from map as well as DLL -- both need to be done
    //      atomically).
    absl::MutexLock lock(&cache_mutex_);
    int overflow = num_entries_ + entries.size() - capacity_;
    if (overflow > 0) {
      if (auto status = Evict(overflow); !status.ok()) {
        PS_VLOG(5) << "Tried to add more entries than total cache capacity: "
                   << status;
      }
    }

    for (const auto& entry : entries) {
      auto it = hash_to_node_.find(entry.first);
      if (it != hash_to_node_.end()) {
        PS_VLOG(5) << "Entry already exists: " << entry.first
                   << ", resetting its expiry";
        struct event* timer_event = it->second->data->timer_event->get();
        evtimer_del(timer_event);
        evtimer_add(timer_event, &expiry_);
        dll_.MoveToFront(it->second);
        continue;
      }

      auto expiry_event_data =
          std::make_unique<ExpiryEventData>(ExpiryEventData{
              .key = entry.first, .k_anon_cache = this, .executor = executor_});
      PS_VLOG(5) << "Setting the entry timeout: "
                 << absl::DurationFromTimeval(expiry_);
      auto data = std::make_unique<
          CacheHashData<KeyT, ValueT>>(CacheHashData<KeyT, ValueT>{
          .key = entry.first,
          .value = entry.second,
          .timer_event = std::make_unique<Event>(
              event_base_.get(), /* fd= */ -1, /* event_type= */ EV_TIMEOUT,
              EntryExpiryCallback,
              /* arg= */ reinterpret_cast<void*>(expiry_event_data.release()),
              kNumEventPriorities, /* event_timeout= */ &expiry_,
              [](struct event* event) {
                // We manually activate the event upon delete so that the
                // memory for argument associated with event callback can
                // always be reclaimed.
                PS_VLOG(6)
                    << "Manually activating the event for memory cleanup";
                // Note: We can't call `event_active` here since it requires
                // the event loop to be running and thus we directly invoke
                // callback ourselves.
                Event::Callback callback = event_get_callback(event);
                callback(-1, 0, event_get_callback_arg(event));
              })});
      Node<KeyT, ValueT>* node = dll_.InsertAtFront(std::move(data));
      hash_to_node_[entry.first] = node;
      ++num_entries_;
      if (num_entries_ == capacity_) {
        PS_VLOG(5)
            << "This batch insert filled cache capacity, skipping "
            << "remaining entries as they will cause eviction of entries "
               "that are also as recent";
        break;
      }
    }
    return absl::OkStatus();
  }

  // Returns all entries (used for testing only).
  absl::flat_hash_map<KeyT, ValueT> GetAllEntriesForTesting()
      ABSL_LOCKS_EXCLUDED(cache_mutex_) {
    absl::flat_hash_map<KeyT, ValueT> entries;
    absl::MutexLock lock(&cache_mutex_);
    for (const auto& [key, node] : hash_to_node_) {
      entries.insert({key, node->data->value});
    }
    return entries;
  }

 protected:
  // Callback to invoke upon an entry expiration.
  static void EntryExpiryCallback(int fd, short event_type, void* arg) {
    PS_VLOG(6) << " " << __func__ << " event type: " << event_type;
    auto expiry_event_data = std::unique_ptr<Cache::ExpiryEventData>(
        reinterpret_cast<Cache::ExpiryEventData*>(arg));
    expiry_event_data->executor->Run(
        [expiry_event_data = std::move(expiry_event_data),
         event_type]() mutable {
          const auto& key = expiry_event_data->key;
          if ((event_type & EV_TIMEOUT) == 0) {
            // Event was manually activated. This is done in order to clean up
            // the `expiry_event_data` passed in argument.
            PS_VLOG(5) << "Event manually activated for key: " << key
                       << ", will reclaim the argument memory";
            return;
          }

          auto* cache = expiry_event_data->k_anon_cache;
          absl::MutexLock lock(&cache->cache_mutex_);
          // Since the Event object destruction will always manually activate
          // the event, we should rely on the final activation to reclaim this
          // user data. Otherwise, we might end up trying to clean up the same
          // memory twice. NOLINTNEXTLINE
          expiry_event_data.release();
          auto& hash_to_node = cache->hash_to_node_;
          auto it = hash_to_node.find(key);
          if (it == hash_to_node.end()) {
            PS_VLOG(5) << "Expiration event for entry: " << key
                       << ", but entry has already been evicted";
            return;
          }

          PS_VLOG(5) << "Expiration event for entry: " << key
                     << ", removing the entry from cache";
          --cache->num_entries_;
          cache->dll_.Remove(it->second);
          hash_to_node.erase(it->first);
        });
  }

  // Notifies about the start of event loop. Helps ensure that the shutdown does
  // not happen before the eventloop is started.
  static void StartedEventLoop(int fd, short event_type, void* arg) {
    auto* self = reinterpret_cast<Cache<KeyT, ValueT>*>(arg);
    PS_VLOG(5) << "Notifying event loop start";
    self->eventloop_started_.Notify();
  }

  // Shuts down the event loop and signals the notification about shutdown.
  static void ShutdownEventLoop(int fd, short event_type, void* arg) {
    auto* self = reinterpret_cast<Cache<KeyT, ValueT>*>(arg);
    if (!self->shutdown_requested_.HasBeenNotified()) {
      return;
    }

    PS_VLOG(5) << "Shutting down the event loop";
    event_base_loopbreak(self->event_base_.get());
    self->shutdown_complete_.Notify();
  }

  // Evicts (at most) `N` entries.
  absl::Status Evict(int N) ABSL_EXCLUSIVE_LOCKS_REQUIRED(cache_mutex_) {
    // Evict N entries from tail of DLL:
    //  - Atomically:
    //    - Remove the timer event for the entry.
    //    - Delete entry from map.
    //    - Delete entry from DLL.
    PS_VLOG(5) << __func__ << ": " << N << " entries";
    Node<KeyT, ValueT>* node = nullptr;
    while ((node = dll_.Tail()) != nullptr && N > 0) {
      PS_VLOG(5) << __func__ << ": Deleting a tail node with key: "
                 << StringifyCacheKeys({node->data->key});
      hash_to_node_.erase(node->data->key);
      dll_.Remove(node);
      --N;
      --num_entries_;
    }
    if (N == 0) {
      return absl::OkStatus();
    }
    return absl::InternalError("No more entries to evict");
  }

 private:
  // Mutex to provide safe access to concurrent threads.
  absl::Mutex cache_mutex_;

  // Structures to help find entries in the cache and keep them sorted based
  // on recency.
  DoublyLinkedList<KeyT, ValueT> dll_ ABSL_GUARDED_BY(cache_mutex_);
  absl::flat_hash_map<KeyT, Node<KeyT, ValueT>*> hash_to_node_
      ABSL_GUARDED_BY(cache_mutex_);

  int num_entries_;
  int capacity_;
  struct timeval expiry_;

  server_common::Executor* executor_;

  absl::AnyInvocable<std::string(const KeyT&, const ValueT&)>
      entry_stringify_func_;

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

  struct ExpiryEventData {
    KeyT key;
    Cache<KeyT, ValueT>* k_anon_cache;
    server_common::Executor* executor;
  };

  // Event loop shut down event checking frequency.
  // We don't want to consume a lot of CPU waiting to detect the event loop
  // shutdown and hence the periodic event time used here is large enough to
  // triggere once per second.
  static inline struct timeval OneSecond = {1, 0};

  // Timer value for the event used to determine that event loop has started.
  // This is used by the constructor to block cache construction so that the
  // caller is sure that once the cache is constructed, Insert calls can proceed
  // without a problem. Note: Since caches are constructed at the start of the
  // service, this delay doesn't affect anything in the critical request path.
  static inline struct timeval OneMicrosecond = {0, 1};

  std::string StringifyCacheKeys(const absl::flat_hash_set<KeyT>& keys) {
    static const ValueT kUnspecified;
    std::string str;
    for (const auto& key : keys) {
      absl::StrAppend(&str, entry_stringify_func_(key, kUnspecified), ",");
    }

    return str;
  }

  std::string StringifyCacheEntries(
      const absl::flat_hash_map<KeyT, ValueT>& entries) {
    std::string str;
    for (const auto& entry : entries) {
      absl::StrAppend(&str, entry_stringify_func_(entry.first, entry.second),
                      ",");
    }

    return str;
  }
};

template <typename KeyT, typename ValueT>
class MockCache : public CacheInterface<KeyT, ValueT> {
 public:
  MockCache() = default;
  ~MockCache() override = default;
  MOCK_METHOD((absl::flat_hash_map<KeyT, ValueT>), Query,
              (const absl::flat_hash_set<KeyT>&), (override));
  MOCK_METHOD(absl::Status, Insert,
              ((const absl::flat_hash_map<KeyT, ValueT>&)), (override));
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_SELLER_FRONTEND_SERVICE_CACHE_CACHE_H_
