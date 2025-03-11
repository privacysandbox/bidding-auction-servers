//  Copyright 2022 Google LLC
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

#ifndef SERVICES_COMMON_CLIENTS_MULTI_CURL_HTTP_FETCHER_ASYNC_H_
#define SERVICES_COMMON_CLIENTS_MULTI_CURL_HTTP_FETCHER_ASYNC_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/multi.h>
#include <event2/event.h>
#include <grpc/event_engine/event_engine.h>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/clients/http/multi_curl_request_manager.h"
#include "services/common/util/event.h"
#include "services/common/util/event_base.h"
#include "src/concurrent/event_engine_executor.h"

namespace privacy_sandbox::bidding_auction_servers {

// Maintains state about the data to upload
struct DataToUpload {
  // JSON string to upload.
  std::string data;
  // First offset in the data that has not yet been uploaded.
  int offset = 0;
};

struct MultiCurlHttpFetcherAsyncOptions {
  const int64_t keepalive_interval_sec = 2;
  const int64_t keepalive_idle_sec = 2;
  std::string ca_cert = "/etc/ssl/certs/ca-certificates.crt";
  const long curlmopt_maxconnects = 0L;
  const long curlmopt_max_total_connections = 0L;
  const long curlmopt_max_host_connections = 0L;
};

// MultiCurlHttpFetcherAsync provides a thread-safe libcurl wrapper to perform
// asynchronous HTTP invocations with client caching(connection pooling), and
// TLS session sharing. It uses a single curl multi handle to perform
// all invoked HTTP actions. It runs a loop in the provided executor
// to provide computation to Libcurl for I/O, and schedules callbacks
// on the provided executor with the result from the HTTP invocation.
// Please note: MultiCurlHttpFetcherAsync makes the best effort to reject any
// calls after the class has started shutting down but does not guarantee
// thread safety if any method is invoked after destruction.
class MultiCurlHttpFetcherAsync final : public HttpFetcherAsync {
 public:
  // Constructs a new multi session for performing HTTP calls.
  // LIBCurl maintains persistent HTTP connections by default.
  // A TCP connection to all request servers is kept warm to reduce TCP
  // handshake latency for each request.
  // If no data is transferred over the TCP connection for keepalive_idle_sec,
  // OS sends a keep alive probe.
  // If the other endpoint does not reply, OS sends another keep alive
  // probe after keepalive_interval_sec.
  explicit MultiCurlHttpFetcherAsync(
      server_common::Executor* executor,
      const MultiCurlHttpFetcherAsyncOptions& options =
          MultiCurlHttpFetcherAsyncOptions{});

  // Cleans up all sessions and errors out any pending open HTTP calls.
  // Please note: Any class using this must ensure that the instance is only
  // destructed when they can ensure that the instance will no longer be invoked
  // from any threads.
  ~MultiCurlHttpFetcherAsync() override
      ABSL_LOCKS_EXCLUDED(in_loop_mu_, curl_handle_set_lock_);

  // Not copyable or movable.
  MultiCurlHttpFetcherAsync(const MultiCurlHttpFetcherAsync&) = delete;
  MultiCurlHttpFetcherAsync& operator=(const MultiCurlHttpFetcherAsync&) =
      delete;

  // Fetches provided url with libcurl.
  //
  // http_request: The URL and headers for the HTTP GET request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. This method guarantees that the callback will be
  // invoked once with the obtained result or error.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrl(const HTTPRequest& request, int timeout_ms,
                OnDoneFetchUrl done_callback) override
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // Fetches provided url and response metadata with libcurl.
  //
  // http_request: The URL and headers for the HTTP GET request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. This method guarantees that the callback will be
  // invoked once with the obtained result or error.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrlWithMetadata(const HTTPRequest& request, int timeout_ms,
                            OnDoneFetchUrlWithMetadata done_callback) override
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // PUTs data to the specified url.
  //
  // http_request: The URL, headers, body for the HTTP PUT request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback will run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // Clients can expect done_callback to be called exactly once.
  void PutUrl(const HTTPRequest& http_request, int timeout_ms,
              OnDoneFetchUrl done_callback) override
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // Fetches provided urls with libcurl.
  //
  // requests: The URL and headers for the HTTP GET requests.
  // timeout: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. This method guarantees that the callback will be
  // invoked once with the obtained result or error. Guaranteed to be invoked
  // with results in the same order as the corresponding requests.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrls(const std::vector<HTTPRequest>& requests,
                 absl::Duration timeout, OnDoneFetchUrls done_callback) override
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // Fetches provided urls and response metadata with libcurl.
  //
  // requests: The URL and headers for the HTTP GET requests.
  // timeout: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. This method guarantees that the callback will be
  // invoked once with the obtained result or error. Guaranteed to be invoked
  // with results in the same order as the corresponding requests.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrlsWithMetadata(const std::vector<HTTPRequest>& requests,
                             absl::Duration timeout,
                             OnDoneFetchUrlsWithMetadata done_callback) override
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

 private:
  // This struct maintains the data related to a Curl request, some of which
  // has to stay valid throughout the life of the request. The code maintains a
  // reference in curl_data_map_ till the request is completed. The destructor
  // is then to free the resources in this class after the request completes.
  struct CurlRequestData {
    // The easy handle provided by libcurl, registered to
    // multi_curl_request_manager_.
    CURL* req_handle;

    // The pointer to the linked list of the request HTTP headers.
    struct curl_slist* headers_list_ptr = nullptr;

    // The callback function for this request from FetchUrl.
    OnDoneFetchUrlWithMetadata done_callback;

    // Pointer to the body of request. Relevant only for HTTP methods that
    // upload data to server.
    std::unique_ptr<DataToUpload> body;

    // Set response headers in the output.
    std::vector<std::string> response_headers;

    // Set the final redirect URL in the output.
    bool include_redirect_url;

    HTTPResponse response_with_metadata;

    CurlRequestData(const std::vector<std::string>& headers,
                    OnDoneFetchUrlWithMetadata on_done,
                    std::vector<std::string> response_header_keys,
                    bool include_redirect_url);
    ~CurlRequestData();
  };

  // FetchUrlsLifetime manages a single FetchUrls request and encapsulates
  // all of the data each individual FetchUrl callback will need.
  struct FetchUrlsWithMetadataLifetime {
    OnDoneFetchUrlsWithMetadata all_done_callback;
    // Results will be passed into all_done_callback.
    std::vector<absl::StatusOr<HTTPResponse>> results;
    // This is used to guard pending_results
    // to keep the count accurate when updated by
    // different threads.
    absl::Mutex results_mu;
    int pending_results;
  };

  // Fetches provided url with libcurl and metadata.
  //
  // http_request: The URL and headers for the HTTP GET request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. This method guarantees that the callback will be
  // invoked once with the obtained result or error.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrl(const HTTPRequest& request, int timeout_ms,
                OnDoneFetchUrlWithMetadata done_callback)
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // This method adds the curl handle to a set to keep track of pending handles.
  // Must be called after the handle has been initialized.
  // Only a single thread can execute this function at a time since it requires
  // the acquisition of the curl_handle_set_lock_ mutex.
  void Add(CURL* handle) ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // This method removes the curl handle if the call has finished or abandoned.
  // Must be called before the handle has been cleaned up.
  // Only a single thread can execute this function at a time since it requires
  // the acquisition of the curl_handle_set_lock_ mutex.
  void Remove(CURL* handle) ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // This method executes PerformCurlUpdate on a loop in the executor_. It
  // will schedule itself as a new task to perform curl check again.
  void ExecuteLoop() ABSL_LOCKS_EXCLUDED(in_loop_mu_);

  // Performs the fetch and handles the response from libcurl. It checks if
  // the req_manager is done performing the fetch. This method provides
  // computation to the underlying Curl Multi to perform I/O and polls for
  // a response. Once the Curl multi interface indicates that a response is
  // available, it schedules the callback on the executor_.
  // Only a single thread can execute this function at a time since it requires
  // the acquisition of the in_loop_mu_ mutex.
  void PerformCurlUpdate() ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // Shuts down the event loop. This is a callback registered with an event
  // that fires every second to see if the event loop should be shutdown.
  static void ShutdownEventLoop(int fd, short event_type, void* arg);

  // Parses curl message to result string or an error message for the callback.
  std::pair<absl::Status, void*> GetResultFromMsg(CURLMsg* msg);

  // Creates and sets up a curl request with default options.
  std::unique_ptr<CurlRequestData> CreateCurlRequest(
      const HTTPRequest& request, int timeout_ms, int64_t keepalive_idle_sec,
      int64_t keepalive_interval_sec, OnDoneFetchUrlWithMetadata done_callback);

  // Adds the request to curl multi request manager. If the addition fails, the
  // executes the callback else stores the request handle in curl data map.
  void ExecuteCurlRequest(std::unique_ptr<CurlRequestData> request)
      ABSL_LOCKS_EXCLUDED(curl_handle_set_lock_);

  // The executor_ will receive tasks from PerformCurlUpdate. The tasks will
  // schedule future ExecuteLoop calls and schedule executions for
  // client callbacks. The executor is not owned by this class instance but is
  // required to outlive the lifetime of this class instance.
  server_common::Executor* executor_;

  // Wait time before sending keepalive probes.
  int64_t keepalive_idle_sec_;

  // Interval time between keep-alive probes in case of no response.
  int64_t keepalive_interval_sec_;

  bool skip_tls_verification_;

  // All events in the loop are associated with this event base. Note: There can
  // be a single event base for a single thread.
  // Documentation: https://libevent.org/libevent-book/Ref2_eventbase.html
  EventBase event_base_;
  // This event is registered with the event loop and fires every second to
  // check if the fetcher has been stopped and if so, the callback registered
  // for this event will also stop the event loop.
  Event shutdown_timer_event_;

  // The multi session used for performing HTTP calls.
  MultiCurlRequestManager multi_curl_request_manager_;
  Event multi_timer_event_;  // Controlled by multi libcurl stack.

  // Path to CA cert roots.pem
  std::string ca_cert_;

  // Makes sure only one execution loop runs at a time.
  absl::Mutex in_loop_mu_;

  // Synchronizes the status of shutdown for destructor and execution loop.
  absl::Notification shutdown_requested_;
  absl::Notification shutdown_complete_;

  // A map of curl easy handles to curl data for easy tracking.
  absl::Mutex curl_handle_set_lock_;
  absl::flat_hash_set<CURL*> curl_handle_set_
      ABSL_GUARDED_BY(curl_handle_set_lock_);
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_MULTI_CURL_HTTP_FETCHER_ASYNC_H_
