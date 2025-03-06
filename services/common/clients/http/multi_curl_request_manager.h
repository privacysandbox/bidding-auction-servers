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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_MULTI_CURL_REQUEST_MANAGER_H_
#define SERVICES_COMMON_CLIENTS_HTTP_MULTI_CURL_REQUEST_MANAGER_H_

#include <event2/event.h>
#include <event2/event_struct.h>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "curl/multi.h"

namespace privacy_sandbox::bidding_auction_servers {

struct SocketInfo {
  // Socket file descriptor.
  curl_socket_t sock_fd;
  // Last activity recorded on the descriptor.
  int activity;
  // Event type monitored by libevent's event loop.
  struct event tracked_event;
};

// This class provides a thread-safe interface around the Curl Multi Interface
// for sharing HTTP resources (connections and TLS session) across
// all HTTP invocations for a client.
// More info: https://curl.se/libcurl/c/threadsafe.html
class MultiCurlRequestManager final {
 public:
  // Initializes the Curl Multi session.
  explicit MultiCurlRequestManager(struct event_base* event_base,
                                   const long curlmopt_maxconnects,
                                   const long curlmopt_max_total_connections,
                                   const long curlmopt_max_host_connections);

  // Configures the request manager with the callback to invoke upon updates
  // to easy handle as well as the timer event to use to trigger transfer on
  // handles.
  void Configure(absl::AnyInvocable<void()> update_easy_handles_callback,
                 struct event* timer_event);
  // This is the callback associated with the timer event which is an input
  // to `Configure` method above.
  static void MultiTimerCallback(int fd, short what, void* arg);
  // This callback is invoked by libcurl when it wants to notify B&A about
  // sockets that B&A should monitor for activity.
  static int OnLibcurlSocketUpdate(CURL* easy_handle, curl_socket_t sock_fd,
                                   int activity, void* data,
                                   void* socket_info_pointer);
  // This callback is invoked by libevent when it detects transfer (read/write)
  // on sockets that libevent was monitoring on behalf of B&A.
  static void OnLibeventSocketActivity(int fd, short kind, void* data);

  // Cleans up the curl mutli session. Please make sure all easy handles
  // related to this multi session are manually cleaned up before this runs.
  ~MultiCurlRequestManager();

  // This method is used to perform I/O work for all/any easy handles
  // related to this multi handle, as a wrapper around curl_multi_perform.
  // It returns any messages from the individual transfers, and acts as a
  // wrapper around the curl_multi_info_read curl method.
  // msgs_left: Output parameter for the number of remaining messages after
  // this function was called.
  CURLMsg* GetUpdate(int* msgs_left) ABSL_LOCKS_EXCLUDED(request_manager_mu_);

  // Add a new curl easy handle to the multi session, and starts the request
  // by calling curl_multi_perform.
  CURLMcode Add(CURL* curl_handle) ABSL_LOCKS_EXCLUDED(request_manager_mu_);

  // Remove a curl easy handle from the multi session by calling
  // curl_multi_remove_handle.
  CURLMcode Remove(CURL* curl_handle) ABSL_LOCKS_EXCLUDED(request_manager_mu_);

  // MultiCurlRequestManager is neither copyable nor movable.
  MultiCurlRequestManager(const MultiCurlRequestManager&) = delete;
  MultiCurlRequestManager& operator=(const MultiCurlRequestManager&) = delete;

 private:
  void UpsertSocketInLibevent(curl_socket_t sock_fd, int activity,
                              SocketInfo* socket_info);
  void AddSocketToLibevent(curl_socket_t sock, int activity, void* data);
  // No. of handles still running updated by curl_multi_perform.
  // This can be used to delay destruction till all easy handles related to this
  // multi have completed running.
  int running_handles_;
  // Mutex for ensuring thread safe access of curl multi session.
  absl::Mutex request_manager_mu_;
  // Actual curl multi session pointer.
  CURLM* request_manager_;
  absl::AnyInvocable<void()> update_easy_handles_callback_;
  struct event_base* event_base_ = nullptr;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_MULTI_CURL_REQUEST_MANAGER_H_
