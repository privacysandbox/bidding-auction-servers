//  Copyright 2025 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_QUEUE_H_
#define SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_QUEUE_H_

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "services/common/cache/doubly_linked_list.h"
#include "services/common/clients/http/curl_request_data.h"
#include "services/common/util/event.h"
#include "services/common/util/event_base.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Request queue used to decouple the producers/reactors who want to make curl
// calls from the executor (a thread that will run the curl transfers).
// Idea is that queuing/dequeing will be a fast operation and reactor threads
// don't have to block (for long periods) to add requests even if the executor
// is taking longer to process any request.
//
// Note: Mutex `Mu()` should be held before accessing any public interface of
// this class.
class CurlRequestQueue {
 public:
  explicit CurlRequestQueue(server_common::Executor* executor, int capacity,
                            absl::Duration max_wait_time);
  ~CurlRequestQueue();

  // Callers can check the fullness before attempting to queue. This is
  // helpful since otherwise callers will try to queue and lose the request
  // data (including any callbacks), if the queue is full.
  bool Full() ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mu());

  // Returns a bool to reflect emptiness of the queue. It helps the executor
  // thread to setup a condition variable and wait for new requests.
  bool Empty() ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mu());

  // Removes element from a queue. Returns nullptr if no element is present.
  // Will be used by the executor thread to get tasks off the queue and
  // timeout thread to remove tasks that have been on the queue for a
  // (configurable) timeout period.
  std::unique_ptr<CurlRequestData> Dequeue()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mu());

  // Adds an element to a queue. Caller must ensure that the queue is not
  // full before calling this method.
  void Enqueue(std::unique_ptr<CurlRequestData> request)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mu());

  // Controls concurrent access to the queue.
  absl::Mutex& Mu();

 private:
  using DLLData = CacheHashData<std::unique_ptr<CurlRequestData>,
                                /*unused=*/char>;
  using NodeType = Node<std::unique_ptr<CurlRequestData>, /*unused=*/char>;

  // Executor is used to run a thread for expiring entries from the queue.
  server_common::Executor* executor_;

  // Signals successful start of event loop.
  static void StartedEventLoop(int fd, short event_type, void* arg);

  // Callback to remove expired entries.
  static void RemoveExpiredEntries(int fd, short event_type, void* arg);

  // Maps from curl request to the node in the doubly linked list.
  absl::flat_hash_map<CurlRequestData*, NodeType*> curl_handles_;

  // Doubly linked list is used to queue/dequeue requests.
  DoublyLinkedList<std::unique_ptr<CurlRequestData>, /*unused=*/char> dll_;

  // Number of elements currently in the queue.
  int size_ = 0;

  // Maximum number of elements that the queue will hold.
  const int capacity_;

  // Mutex used to expire entries from the queue.
  absl::Mutex mu_;

  // Event base used to register the request expiry events.
  EventBase event_base_ = EventBase(/*use_pthreads=*/true);

  // Max time a request is allowed to wait in the queue before it is expired.
  absl::Duration max_wait_;

  // Maximum time to wait for the request to be dequeued before the request
  // forcibly expired from the queue.
  struct timeval expiry_;

  // Activates on event loop start and signals that event loop has successfully
  // started.
  Event eventloop_started_event_;

  // Callback executed every `max_wait_` time to remove expired entries off
  // the queue.
  Event ticker_;

  // Notification to sync the caller with the event loop start.
  absl::Notification eventloop_started_;

  // Once set all the incoming enqueue requests will be rejected right away.
  absl::Notification shutting_down_;
  absl::Notification shutdown_completed_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_QUEUE_H_
