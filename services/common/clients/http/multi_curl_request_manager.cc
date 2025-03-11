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

#include "services/common/clients/http/multi_curl_request_manager.h"

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <event2/event.h>
#include <event2/util.h>

#include "absl/log/check.h"
#include "src/logger/request_context_logger.h"

namespace privacy_sandbox::bidding_auction_servers {

namespace {

using EventCallback = void (*)(int, short, void*);

}  // namespace

void RemoveSocketFromLibevent(std::unique_ptr<SocketInfo> socket_info) {
  if (!socket_info) {
    return;
  }

  if (event_initialized(&socket_info->tracked_event)) {
    // Make the event non-pending and non-active i.e.
    // remove the registered event from the libevent's monitoring.
    event_del(&socket_info->tracked_event);
  }
}

void MultiCurlRequestManager::OnLibeventSocketActivity(int fd, short kind,
                                                       void* data) {
  int action = ((kind & EV_READ) ? CURL_CSELECT_IN : 0) |
               ((kind & EV_WRITE) ? CURL_CSELECT_OUT : 0);

  auto* self = reinterpret_cast<MultiCurlRequestManager*>(data);
  {
    absl::MutexLock l(&self->request_manager_mu_);
    curl_multi_socket_action(self->request_manager_, fd, action,
                             &self->running_handles_);
  }
  self->update_easy_handles_callback_();
}

void MultiCurlRequestManager::UpsertSocketInLibevent(curl_socket_t sock_fd,
                                                     int activity,
                                                     SocketInfo* socket_info) {
  int kind = ((activity & CURL_POLL_IN) ? EV_READ : 0) |
             ((activity & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST;

  socket_info->sock_fd = sock_fd;
  socket_info->activity = activity;

  if (event_initialized(&socket_info->tracked_event)) {
    event_del(&socket_info->tracked_event);
  }

  event_assign(&socket_info->tracked_event, event_base_, sock_fd, kind,
               OnLibeventSocketActivity, this);
  event_add(&socket_info->tracked_event, /*timeout=*/nullptr);
}

void MultiCurlRequestManager::AddSocketToLibevent(curl_socket_t sock,
                                                  int activity, void* data) {
  auto socket_info = std::make_unique<SocketInfo>();
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(data);
  self->UpsertSocketInLibevent(sock, activity, socket_info.get());
  curl_multi_assign(self->request_manager_, sock, socket_info.release());
}

int MultiCurlRequestManager::OnLibcurlSocketUpdate(CURL* easy_handle,
                                                   curl_socket_t sock_fd,
                                                   int activity, void* data,
                                                   void* socket_info_pointer) {
  struct SocketInfo* socket_info =
      reinterpret_cast<struct SocketInfo*>(socket_info_pointer);
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(data);
  // See activity details here:
  // https://curl.se/libcurl/c/CURLMOPT_SOCKETFUNCTION.html
  if (activity == CURL_POLL_REMOVE) {
    RemoveSocketFromLibevent(std::unique_ptr<SocketInfo>(socket_info));
  } else if (!socket_info) {
    self->AddSocketToLibevent(sock_fd, activity, data);
  } else {
    self->UpsertSocketInLibevent(sock_fd, activity, socket_info);
  }
  return 0;
}

int OnLibcurlTimerUpdate(CURLM* multi, long timeout_ms, void* timer_event_arg) {
  DCHECK_NE(timer_event_arg, nullptr) << "Timer event not found";
  struct event* timer_event = reinterpret_cast<struct event*>(timer_event_arg);
  if (timeout_ms == -1) {
    evtimer_del(timer_event);
  } else {
    struct timeval timeout = {.tv_sec = timeout_ms / 1000,
                              .tv_usec = (timeout_ms % 1000) * 1000};
    evtimer_add(timer_event, &timeout);
  }
  return 0;
}

MultiCurlRequestManager::MultiCurlRequestManager(
    struct event_base* event_base, const long curlmopt_maxconnects,
    const long curlmopt_max_total_connections,
    const long curlmopt_max_host_connections)
    : event_base_(event_base) {
  running_handles_ = 0;
  curl_global_init(CURL_GLOBAL_ALL);
  request_manager_ = curl_multi_init();
  curl_multi_setopt(request_manager_, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(request_manager_, CURLMOPT_SOCKETFUNCTION,
                    OnLibcurlSocketUpdate);
  if (curlmopt_maxconnects > 0) {
    // Limits number of connections left alive in cache.
    curl_multi_setopt(request_manager_, CURLMOPT_MAXCONNECTS,
                      curlmopt_maxconnects);
  }
  // Limits number of connections allowed active.
  curl_multi_setopt(request_manager_, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                    curlmopt_max_total_connections);
  // The maximum amount of simultaneously open connections libcurl may hold
  // to a single host.
  curl_multi_setopt(request_manager_, CURLMOPT_MAX_HOST_CONNECTIONS,
                    curlmopt_max_host_connections);
}

MultiCurlRequestManager::~MultiCurlRequestManager() {
  // Cancel all requests and exit.
  curl_multi_cleanup(request_manager_);
  curl_global_cleanup();
}

// static
void MultiCurlRequestManager::MultiTimerCallback(int fd, short what,
                                                 void* arg) {
  auto* self = reinterpret_cast<MultiCurlRequestManager*>(arg);
  {
    absl::MutexLock l(&self->request_manager_mu_);
    curl_multi_socket_action(self->request_manager_, CURL_SOCKET_TIMEOUT, 0,
                             &self->running_handles_);
  }
  self->update_easy_handles_callback_();
}

void MultiCurlRequestManager::Configure(
    absl::AnyInvocable<void()> update_easy_handles_callback,
    struct event* timer_event) {
  update_easy_handles_callback_ = std::move(update_easy_handles_callback);
  curl_multi_setopt(request_manager_, CURLMOPT_TIMERDATA, timer_event);
  curl_multi_setopt(request_manager_, CURLMOPT_TIMERFUNCTION,
                    OnLibcurlTimerUpdate);
}

CURLMcode MultiCurlRequestManager::Add(CURL* curl_handle)
    ABSL_LOCKS_EXCLUDED(request_manager_mu_) {
  absl::MutexLock l(&request_manager_mu_);
  return curl_multi_add_handle(request_manager_, curl_handle);
}

CURLMsg* MultiCurlRequestManager::GetUpdate(int* msgs_left)
    ABSL_LOCKS_EXCLUDED(request_manager_mu_) {
  absl::MutexLock l(&request_manager_mu_);
  curl_multi_perform(request_manager_, &running_handles_);
  CURLMsg* msg = curl_multi_info_read(request_manager_, msgs_left);
  return msg;
}

CURLMcode MultiCurlRequestManager::Remove(CURL* curl_handle)
    ABSL_LOCKS_EXCLUDED(request_manager_mu_) {
  absl::MutexLock l(&request_manager_mu_);
  return curl_multi_remove_handle(request_manager_, curl_handle);
}

}  // namespace privacy_sandbox::bidding_auction_servers
