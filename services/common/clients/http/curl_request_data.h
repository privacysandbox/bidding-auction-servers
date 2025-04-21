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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_DATA_H_
#define SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_DATA_H_

#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace privacy_sandbox::bidding_auction_servers {

using OnDoneFetchUrl = absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>;
using OnDoneFetchUrls =
    absl::AnyInvocable<void(std::vector<absl::StatusOr<std::string>>) &&>;

struct HTTPRequest {
  std::string url;
  // Optional
  std::vector<std::string> headers = {};
  // Optional
  std::string body = "";

  // Will include response headers in the response with the specified
  // case insensitive name.
  // Defaults to empty.
  std::vector<std::string> include_headers = {};

  struct RedirectConfig {
    // Limits redirects to HTTP/HTTPS only.
    // Defaults to false.
    bool strict_http = false;

    // Get redirect URL in response metadata.
    // Defaults to false.
    bool get_redirect_url = false;
  } redirect_config;
};

struct HTTPResponse {
  // The pointer to this is used to write the request output.
  std::string body;

  // Optional. Included if include_headers has values.
  absl::flat_hash_map<std::string, absl::StatusOr<std::string>> headers;

  // Optional. Included if get_redirect_url is true.
  std::string final_url;
};

// Maintains state about the data to upload vi CURL.
struct DataToUpload {
  // JSON string to upload.
  std::string data;
  // First offset in the data that has not yet been uploaded.
  int offset = 0;
};

using OnDoneFetchUrlWithMetadata =
    absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>) &&>;
using OnDoneFetchUrlsWithMetadata =
    absl::AnyInvocable<void(std::vector<absl::StatusOr<HTTPResponse>>) &&>;

// Maintains the information about the socket associated with each curl request.
struct SocketInfo {
  // Socket file descriptor.
  curl_socket_t sock_fd;
  // Last activity recorded on the descriptor.
  int activity;
  // Event type monitored by libevent's event loop.
  struct event tracked_event;
};

// Maximum default number of pending curl requets that are awaiting processing.
constexpr inline int kDefaultMaxCurlPendingRequests = 5000;
// Maximum default wait time that a request can sit in the processing queue,
// after which the request will be removed from the queue.
constexpr inline absl::Duration kDefaultMaxRequestWaitTime =
    absl::Milliseconds(100);
// Default number of curl workers to use to process the curl requests.
constexpr inline int kDefaultNumCurlWorkers = 1;

struct MultiCurlHttpFetcherAsyncOptions {
  const int64_t keepalive_interval_sec = 2;
  const int64_t keepalive_idle_sec = 2;
  std::string ca_cert = "/etc/ssl/certs/ca-certificates.crt";
  const long curlmopt_maxconnects = 0L;
  const long curlmopt_max_total_connections = 0L;
  const long curlmopt_max_host_connections = 0L;
  int num_curl_workers = kDefaultNumCurlWorkers;
  absl::Duration curl_max_wait_time_ms = kDefaultMaxRequestWaitTime;
  int curl_queue_length = kDefaultMaxCurlPendingRequests;
};

// This struct maintains the data related to a Curl request, some of which
// has to stay valid throughout the life of the request. The code maintains a
// reference in curl_data_map_ till the request is completed. The destructor
// is then to free the resources in this class after the request completes.
struct CurlRequestData {
  explicit CurlRequestData(const std::vector<std::string>& headers,
                           OnDoneFetchUrlWithMetadata on_done,
                           std::vector<std::string> response_header_keys,
                           bool include_redirect_url);
  ~CurlRequestData();

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

  // Records the time when the request was started.
  absl::Time start_time;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_CURL_REQUEST_DATA_H_
