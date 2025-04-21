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

#include <memory>

#include <event2/event.h>
#include <event2/event_struct.h>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "curl/multi.h"
#include "services/common/clients/http/curl_request_data.h"
#include "services/common/util/event.h"
#include "services/common/util/event_base.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class provides a thread-safe interface around the Curl Multi Interface
// for sharing HTTP resources (connections and TLS session) across
// all HTTP invocations for a client.
// More info: https://curl.se/libcurl/c/threadsafe.html
class MultiCurlRequestManager final {
 public:
  // Initializes the Curl Multi session.
  explicit MultiCurlRequestManager(const long curlmopt_maxconnects,
                                   const long curlmopt_max_total_connections,
                                   const long curlmopt_max_host_connections,
                                   server_common::Executor& executor);

  // Cleans up the curl mutli session. Please make sure all easy handles
  // related to this multi session are manually cleaned up before this runs.
  ~MultiCurlRequestManager();

  // MultiCurlRequestManager is neither copyable nor movable.
  MultiCurlRequestManager(const MultiCurlRequestManager&) = delete;
  MultiCurlRequestManager& operator=(const MultiCurlRequestManager&) = delete;

  // Adds the request to curl multi request manager. If the addition fails,
  // then executes the callback, else stores the request handle.
  void StartProcessing(std::unique_ptr<CurlRequestData> request);

 private:
  void UpsertSocketInLibevent(curl_socket_t sock_fd, int activity,
                              SocketInfo* socket_info);
  void AddSocketToLibevent(curl_socket_t sock, int activity, void* data);

  // Signals successful start of event loop.
  static void StartedEventLoop(int fd, short event_type, void* arg);

  // Callback for the event loop to check whether shutdown has triggered.
  static void ShutdownEventLoop(int fd, short event_type, void* arg);

  // Callback when the event loop is ready to start processing the request.
  static void OnProcessingStarted(int fd, short event_type, void* arg);

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

  // Adds a new curl easy handle to the multi session, and starts the request
  // by calling curl_multi_perform.
  CURLMcode Add(CURL* curl_handle);

  // Removes a curl easy handle from the multi session by calling
  // curl_multi_remove_handle.
  std::unique_ptr<CurlRequestData> Remove(CURL* curl_handle);

  // Performs the fetch and handles the response from libcurl. It checks if
  // the req_manager is done performing the fetch. This method provides
  // computation to the underlying Curl Multi to perform I/O and polls for
  // a response. Once the Curl multi interface indicates that a response is
  // available, it schedules the callback on the executor_.
  // Only a single thread can execute this function at a time since it requires
  // the acquisition of the in_loop_mu_ mutex.
  void PerformCurlUpdate();

  // Map from easy handle pointer to curl request data. This is used to ensure
  // proper cleanup (even in case of errors where the curl transfer fails).
  absl::flat_hash_map<CURL*, std::unique_ptr<CurlRequestData>>
      easy_curl_request_data_;
  // No. of handles still running updated by curl_multi_perform.
  // This can be used to delay destruction till all easy handles related to this
  // multi have completed running.
  int running_handles_;
  // Actual curl multi session pointer.
  CURLM* request_manager_;
  EventBase event_base_;
  // Activates on event loop start and signals that event loop has successfully
  // started.
  Event eventloop_started_event_;
  // This event is registered with the event loop and fires every second to
  // check if the fetcher has been stopped and if so, the callback registered
  // for this event will also stop the event loop.
  Event shutdown_timer_event_;
  // A timer event controlled by multi libcurl stack to orchestrate the request
  // processing.
  Event multi_timer_event_;

  // Notification to sync the caller with the event loop start.
  absl::Notification eventloop_started_;

  // Synchronizes the status of shutdown for destructor and execution loop.
  absl::Notification shutdown_complete_;

  server_common::Executor& executor_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_MULTI_CURL_REQUEST_MANAGER_H_
