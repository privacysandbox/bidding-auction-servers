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

#include "absl/synchronization/mutex.h"
#include "curl/multi.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class provides a thread-safe interface around the Curl Multi Interface
// for sharing HTTP resources (connections and TLS session) across
// all HTTP invocations for a client.
// More info: https://curl.se/libcurl/c/threadsafe.html
class MultiCurlRequestManager final {
 public:
  // Initializes the Curl Multi session.
  MultiCurlRequestManager();
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
  // No. of handles still running updated by curl_multi_perform.
  // This can be used to delay destruction till all easy handles related to this
  // multi have completed running.
  int running_handles_;
  // Mutex for ensuring thread safe access of curl multi session.
  absl::Mutex request_manager_mu_;
  // Actual curl multi session pointer.
  CURLM* request_manager_ ABSL_GUARDED_BY(request_manager_mu_);
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_MULTI_CURL_REQUEST_MANAGER_H_
