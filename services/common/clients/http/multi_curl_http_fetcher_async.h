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
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <curl/multi.h>
#include <grpc/event_engine/event_engine.h>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "services/common/clients/http/curl_request_data.h"
#include "services/common/clients/http/curl_request_queue.h"
#include "services/common/clients/http/curl_request_worker.h"
#include "services/common/clients/http/http_fetcher_async.h"
#include "services/common/util/event.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers {

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
  // from any thread.
  ~MultiCurlHttpFetcherAsync() override = default;

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
  // invoked once with the obtained result or error. This method is thread safe.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrl(const HTTPRequest& request, int timeout_ms,
                OnDoneFetchUrl done_callback) override;

  // Fetches provided url and response metadata with libcurl.
  //
  // http_request: The URL and headers for the HTTP GET request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. This method guarantees that the callback will be
  // invoked once with the obtained result or error. This method is thread safe.
  // Please note: done_callback will run in a threadpool and is not guaranteed
  // to be the FetchUrl client's thread.
  void FetchUrlWithMetadata(const HTTPRequest& request, int timeout_ms,
                            OnDoneFetchUrlWithMetadata done_callback) override;

  // PUTs data to the specified url.
  //
  // http_request: The URL, headers, body for the HTTP PUT request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback will run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // This method is thread safe.
  // Clients can expect done_callback to be called exactly once.
  void PutUrl(const HTTPRequest& http_request, int timeout_ms,
              OnDoneFetchUrl done_callback) override;

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
                 absl::Duration timeout,
                 OnDoneFetchUrls done_callback) override;

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
  void FetchUrlsWithMetadata(
      const std::vector<HTTPRequest>& requests, absl::Duration timeout,
      OnDoneFetchUrlsWithMetadata done_callback) override;

 private:
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
                OnDoneFetchUrlWithMetadata done_callback);

  // Creates and sets up a curl request with default options.
  std::unique_ptr<CurlRequestData> CreateCurlRequest(
      const HTTPRequest& request, int timeout_ms, int64_t keepalive_idle_sec,
      int64_t keepalive_interval_sec, OnDoneFetchUrlWithMetadata done_callback);

  // Puts the curl request on the request queue.
  void ScheduleAsyncCurlRequest(std::unique_ptr<CurlRequestData> request);

  // The executor_ will be used to run the processing thread that adds handles
  // to multi curl as well as runs the multi handle to complete the data
  // transfers.
  server_common::Executor* executor_;

  // Wait time before sending keepalive probes.
  int64_t keepalive_idle_sec_;

  // Interval time between keep-alive probes in case of no response.
  int64_t keepalive_interval_sec_;

  bool skip_tls_verification_;

  // Path to CA cert roots.pem
  std::string ca_cert_;

  // CA cert blob holding the contents of roots.pem.
  std::string ca_cert_blob_;

  // Synchronizes the status of shutdown for destructor and execution loop.
  bool shutdown_requested_ = false;
  absl::Notification shutdown_complete_;

  const int num_curl_workers_;

  std::vector<std::unique_ptr<CurlRequestQueue>> request_queues_;
  std::vector<std::unique_ptr<CurlRequestWorker>> curl_request_workers_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_MULTI_CURL_HTTP_FETCHER_ASYNC_H_
