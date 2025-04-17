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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_WORKER_H_
#define SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_WORKER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "absl/synchronization/notification.h"
#include "services/common/clients/http/curl_request_data.h"
#include "services/common/clients/http/curl_request_queue.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/clients/http/multi_curl_request_manager.h"
#include "services/common/loggers/request_log_context.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Worker that pulls off the request from the passed in queue.
class CurlRequestWorker {
 public:
  explicit CurlRequestWorker(server_common::Executor* executor,
                             CurlRequestQueue& request_queue,
                             const long curlmopt_maxconnects = 0,
                             const long curlmopt_max_total_connections = 0,
                             const long curlmopt_max_host_connections = 0);

  ~CurlRequestWorker();

 private:
  // Wraps the data needed by the dispatcher thread to decide whether it
  // can do any processing.
  struct CurlRequestWaiterArg {
    CurlRequestQueue& queue;
    bool& shutdown_requested;
  };

  // Executes a thread to run the processing loop.
  server_common::Executor* executor_;

  // Request queue to monitor for incoming requests.
  CurlRequestQueue& request_queue_;

  // Used to decide when to break out of the processing loop.
  bool shutdown_requested_ = false;

  // Signaled by processingl loop when it is safe to destruct the object.
  absl::Notification shutdown_complete_;

  // The multi session used for performing HTTP calls.
  MultiCurlRequestManager multi_curl_request_manager_;

  // Pulls a requests off the queue and executes it, otherwise blocks
  // for a request to arrive.
  void ProcessRequests();
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_WORKER_H_
