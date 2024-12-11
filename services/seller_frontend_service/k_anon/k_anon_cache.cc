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

#include "services/seller_frontend_service/k_anon/k_anon_cache.h"

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

// Data needed for entry expiration callback.
struct ExpiryEventData {
  std::string hash;
  KAnonCache* k_anon_cache;
};

// Event loop shut down event checking frequency.
// We don't want to consume a lot of CPU waiting to detect the event loop
// shutdown and hence the periodic event time used here is large enough to
// triggere once per second.
static struct timeval OneSecond = {1, 0};

// Timer value for the event used to determine that event loop has started.
// This is used by the constructor to block cache construction so that the
// caller is sure that once the cache is constructed, Insert calls can proceed
// without a problem. Note: Since caches are constructed at the start of the
// service, this delay doesn't affect anything in the critical request path.
static struct timeval OneMicrosecond = {0, 1};

}  // namespace

KAnonCache::KAnonCache(int capacity, absl::Duration ttl,
                       server_common::Executor* executor)
    : num_entries_(0),
      capacity_(capacity),
      expiry_(absl::ToTimeval(ttl)),
      executor_(executor),
      eventloop_started_event_(Event(event_base_.get(), /*fd=*/-1,
                                     /*event_type=*/0,
                                     /*event_callback=*/StartedEventLoop,
                                     /*arg=*/this,
                                     /*priority=*/0, &OneMicrosecond)),
      shutdown_timer_event_(Event(event_base_.get(), /*fd=*/-1,
                                  /*event_type=*/EV_PERSIST,
                                  /*event_callback=*/ShutdownEventLoop,
                                  /*arg=*/this,
                                  /*priority=*/0, &OneSecond)) {
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

absl::flat_hash_set<std::string> KAnonCache::Query(
    const absl::flat_hash_set<std::string>& hashes)
    ABSL_LOCKS_EXCLUDED(cache_mutex_) {
  PS_VLOG(5) << " " << __func__ << ": " << absl::StrJoin(hashes, ", ");
  // For each hash:
  //  Lookup the hash in map:
  //    If found in map (then atomically):
  //    - Swap the node in the DLL with the front node.
  //    - Put the found hash into the set to return (can be done without lock).
  absl::flat_hash_set<std::string> found_hashes;
  absl::MutexLock lock(&cache_mutex_);
  for (const auto& hash : hashes) {
    auto it = hash_to_node_.find(hash);
    if (it == hash_to_node_.end()) {
      continue;
    }

    dll_.MoveToFront(it->second);
    found_hashes.insert(hash);
  }
  return found_hashes;
}

// static
void KAnonCache::StartedEventLoop(int fd, short event_type, void* arg) {
  auto* self = reinterpret_cast<KAnonCache*>(arg);
  PS_VLOG(5) << "Notifying event loop start";
  self->eventloop_started_.Notify();
}

// static
void KAnonCache::ShutdownEventLoop(int fd, short event_type, void* arg) {
  auto* self = reinterpret_cast<KAnonCache*>(arg);
  if (!self->shutdown_requested_.HasBeenNotified()) {
    return;
  }

  PS_VLOG(5) << "Shutting down the event loop";
  event_base_loopbreak(self->event_base_.get());
  self->shutdown_complete_.Notify();
}

KAnonCache::~KAnonCache() {
  PS_VLOG(6) << "Ensuring that event loop has started before trying to stop it";
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

// static
void KAnonCache::EntryExpiryCallback(int fd, short event_type, void* arg) {
  // TODO: Pass this method information about which node this timer is attached
  // to and how to get the corresponding hash and access map + DLL.
  PS_VLOG(6) << " " << __func__ << " event type: " << event_type;
  auto expiry_event_data =
      std::unique_ptr<ExpiryEventData>(reinterpret_cast<ExpiryEventData*>(arg));
  const auto& hash = expiry_event_data->hash;
  if ((event_type & EV_TIMEOUT) == 0) {
    // Event was manually activated. This is done in order to clean up the
    // passed in argument.
    PS_VLOG(5) << "Event manually activated for hash: " << hash
               << ", will reclaim the argument memory";
    return;
  }

  auto* cache = expiry_event_data->k_anon_cache;
  absl::MutexLock lock(&cache->cache_mutex_);
  PS_VLOG(5) << " " << __func__;
  // Since the Event object destruction will always manually activate the event,
  // we should rely on the final activation to reclaim this user data.
  // Otherwise, we might end up trying to clean up the same memory twice.
  // NOLINTNEXTLINE
  expiry_event_data.release();
  auto& hash_to_node = cache->hash_to_node_;
  auto it = hash_to_node.find(hash);
  if (it == hash_to_node.end()) {
    PS_VLOG(5) << "Expiration event for hash: " << hash
               << ", but entry has already been evicted";
    return;
  }
  PS_VLOG(5) << "Expiration event for hash: " << hash << ", removing the "
             << "entry from cache";
  --cache->num_entries_;
  cache->dll_.Remove(it->second);
  hash_to_node.erase(it->first);
}

absl::Status KAnonCache::Insert(const absl::flat_hash_set<std::string>& hashes)
    ABSL_LOCKS_EXCLUDED(cache_mutex_) {
  // For each hash:
  //  - Check if we are under capacity.
  //  - Evict an LRU entry if over capacity.
  //  - Update DLL and map with new node (Need writer mutex lock).
  //  - Insert an ephemeral timer event in event loop (with callack set to
  //      delete that entry from map as well as DLL -- both need to be done
  //      atomically).
  absl::MutexLock lock(&cache_mutex_);
  PS_VLOG(5) << " " << __func__ << ": " << absl::StrJoin(hashes, ", ");
  int overflow = num_entries_ + hashes.size() - capacity_;
  if (overflow > 0) {
    PS_RETURN_IF_ERROR(Evict(overflow));
  }

  for (const auto& hash : hashes) {
    auto it = hash_to_node_.find(hash);
    if (it != hash_to_node_.end()) {
      PS_VLOG(5) << "Hash already exists: " << hash << ", resetting its expiry";
      struct event* timer_event = it->second->data->timer_event->get();
      evtimer_del(timer_event);
      evtimer_add(timer_event, &expiry_);
      dll_.MoveToFront(it->second);
      continue;
    }

    auto expiry_event_data = std::make_unique<ExpiryEventData>(
        ExpiryEventData{.hash = hash, .k_anon_cache = this});
    PS_VLOG(5) << "Setting the hash entry timeout: "
               << absl::DurationFromTimeval(expiry_);
    auto data = std::make_unique<KAnonHashData>(KAnonHashData{
        .hash = hash,
        .timer_event = std::make_unique<Event>(
            event_base_.get(), /*fd=*/-1, /*event_type=*/EV_TIMEOUT,
            EntryExpiryCallback,
            /*arg=*/reinterpret_cast<void*>(expiry_event_data.release()),
            kNumEventPriorities, /*event_timeout=*/&expiry_,
            [](struct event* event) {
              // We manually activate the event upon delete so that the memory
              // for argument associated with event callback can always be
              // reclaimed.
              PS_VLOG(6) << "Manually activating the event for memory cleanup";
              // Note: We can't call `event_active` here since it requires the
              // event loop to be running and thus we directly invoke callback
              // ourselves.
              Event::Callback callback = event_get_callback(event);
              callback(-1, 0, event_get_callback_arg(event));
            })});
    Node* node = dll_.InsertAtFront(std::move(data));
    hash_to_node_[hash] = node;
    ++num_entries_;
  }
  return absl::OkStatus();
}

absl::Status KAnonCache::Evict(int N)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(cache_mutex_) {
  // Evict N entries from tail of DLL:
  //  - Atomically:
  //    - Remove the timer event for the entry.
  //    - Delete entry from map.
  //    - Delete entry from DLL.
  PS_VLOG(5) << __func__ << ": " << N << " entries";
  Node* node = nullptr;
  while ((node = dll_.Tail()) != nullptr && N > 0) {
    PS_VLOG(5) << __func__
               << ": Deleting a tail node with hash: " << node->data->hash;
    hash_to_node_.erase(node->data->hash);
    dll_.Remove(node);
    --N;
    --num_entries_;
  }
  if (N == 0) {
    return absl::OkStatus();
  }
  return absl::InternalError("No more entries to evict");
}

absl::flat_hash_set<std::string> KAnonCache::GetAllHashesForTesting()
    ABSL_LOCKS_EXCLUDED(cache_mutex_) {
  absl::flat_hash_set<std::string> hashes;
  absl::MutexLock lock(&cache_mutex_);
  for (const auto& [hash, node] : hash_to_node_) {
    hashes.insert(hash);
  }
  return hashes;
}

}  // namespace privacy_sandbox::bidding_auction_servers
