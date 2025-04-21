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

#include "services/common/clients/http/curl_request_queue.h"

#include <string>
#include <utility>

#include "absl/log/check.h"
#include "services/common/cache/doubly_linked_list.h"
#include "services/common/util/event.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {
// Timer used by the event on event loop to signal that event loop has
// successfully started.
inline static timeval OneMicrosecond = {0, 1};

}  // namespace

CurlRequestQueue::CurlRequestQueue(server_common::Executor* executor,
                                   int capacity, absl::Duration max_wait_time)
    : executor_(executor),
      capacity_(capacity),
      max_wait_(max_wait_time),
      expiry_(absl::ToTimeval(max_wait_time)),
      eventloop_started_event_(Event(event_base_.get(), /* fd= */ -1,
                                     /* event_type= */ 0,
                                     /* event_callback= */ StartedEventLoop,
                                     /* arg= */ this,
                                     /* priority= */ kNumEventPriorities / 2,
                                     &OneMicrosecond)),
      ticker_(Event(event_base_.get(), /*fd=*/-1,
                    /*event_type=*/EV_PERSIST,
                    /*event_callback=*/RemoveExpiredEntries,
                    /*arg=*/this,
                    /*priority=*/kNumEventPriorities / 2, &expiry_)) {
  DCHECK(executor_ != nullptr);
  // Start the event loop - used to remove expired request from the request
  // queue.
  executor_->Run([this]() { event_base_dispatch(event_base_.get()); });
  eventloop_started_.WaitForNotification();
}

absl::Mutex& CurlRequestQueue::Mu() { return mu_; }

bool CurlRequestQueue::Full() { return size_ == capacity_; }

bool CurlRequestQueue::Empty() { return size_ == 0; }

std::unique_ptr<CurlRequestData> CurlRequestQueue::Dequeue() {
  if (Empty()) {
    return nullptr;
  }

  --size_;
  auto* node = dll_.Tail();
  std::unique_ptr<CurlRequestData> curl_request_data =
      std::move(node->data->key);
  dll_.Remove(curl_handles_[curl_request_data.get()]);
  curl_handles_.erase(curl_request_data.get());
  return curl_request_data;
}

// static
void CurlRequestQueue::StartedEventLoop(int fd, short event_type, void* arg) {
  auto* self = reinterpret_cast<CurlRequestQueue*>(arg);
  PS_VLOG(5) << "Notifying event loop start";
  self->eventloop_started_.Notify();
}

// static
void CurlRequestQueue::RemoveExpiredEntries(int fd, short event_type,
                                            void* arg) {
  auto* self = reinterpret_cast<CurlRequestQueue*>(arg);
  if (self->shutting_down_.HasBeenNotified()) {
    absl::MutexLock lock(&self->Mu());
    while (auto request = self->Dequeue()) {
      std::move(request->done_callback)(absl::InternalError("Shutting down."));
    }
    event_base_loopbreak(self->event_base_.get());
    self->shutdown_completed_.Notify();
    return;
  }

  absl::MutexLock lock(&self->mu_);
  while (auto* node = self->dll_.Head()) {
    auto* curl_request_data_ptr = node->data->key.get();
    if (absl::Now() - curl_request_data_ptr->start_time < self->max_wait_) {
      return;
    }

    --self->size_;
    std::unique_ptr<CurlRequestData> curl_request_data =
        std::move(node->data->key);
    self->dll_.Remove(self->curl_handles_[curl_request_data_ptr]);
    self->curl_handles_.erase(curl_request_data_ptr);

    // Move the callback to a different thread.
    self->executor_->Run([curl_request_data = std::move(curl_request_data)]() {
      std::move(curl_request_data->done_callback)(
          absl::InternalError("Request timed out waiting in the queue"));
    });
  }
}

void CurlRequestQueue::Enqueue(std::unique_ptr<CurlRequestData> request) {
  if (shutting_down_.HasBeenNotified()) {
    std::move(request->done_callback)(absl::InternalError("Shutting down."));
    return;
  }

  auto* curl_request_data = request.get();
  curl_request_data->start_time = absl::Now();
  auto data = std::make_unique<DLLData>(DLLData{.key = std::move(request)});
  auto* dll_node = dll_.InsertAtFront(std::move(data));
  curl_handles_[curl_request_data] = dll_node;
  ++size_;
}

CurlRequestQueue::~CurlRequestQueue() {
  shutting_down_.Notify();
  event_active(ticker_.get(), /*res=*/0, /*ncalls=*/0);
  shutdown_completed_.WaitForNotification();
}

}  // namespace privacy_sandbox::bidding_auction_servers
