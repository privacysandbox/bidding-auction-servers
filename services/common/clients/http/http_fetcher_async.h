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

#ifndef SERVICES_COMMON_CLIENTS_HTTP_FETCHER_ASYNC_H_
#define SERVICES_COMMON_CLIENTS_HTTP_FETCHER_ASYNC_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace privacy_sandbox::bidding_auction_servers {

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

using OnDoneFetchUrl = absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>;
using OnDoneFetchUrls =
    absl::AnyInvocable<void(std::vector<absl::StatusOr<std::string>>) &&>;

using OnDoneFetchUrlWithMetadata =
    absl::AnyInvocable<void(absl::StatusOr<HTTPResponse>) &&>;
using OnDoneFetchUrlsWithMetadata =
    absl::AnyInvocable<void(std::vector<absl::StatusOr<HTTPResponse>>) &&>;

class HttpFetcherAsync {
 public:
  HttpFetcherAsync() = default;
  virtual ~HttpFetcherAsync() = default;
  HttpFetcherAsync(const HttpFetcherAsync&) = delete;
  HttpFetcherAsync& operator=(const HttpFetcherAsync&) = delete;

  // Fetches the specified url.
  //
  // http_request: The URL and headers for the HTTP GET request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback will run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // Clients can expect done_callback to be called exactly once.
  virtual void FetchUrl(const HTTPRequest& http_request, int timeout_ms,
                        OnDoneFetchUrl done_callback) = 0;

  // Fetches the specified url with response metadata.
  //
  // http_request: The URL and headers for the HTTP GET request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback will run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // Clients can expect done_callback to be called exactly once.
  virtual void FetchUrlWithMetadata(
      const HTTPRequest& http_request, int timeout_ms,
      OnDoneFetchUrlWithMetadata done_callback) = 0;

  // PUTs data to the specified url.
  //
  // http_request: The URL, headers, body for the HTTP PUT request.
  // timeout_ms: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback will run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // Clients can expect done_callback to be called exactly once.
  virtual void PutUrl(const HTTPRequest& http_request, int timeout_ms,
                      OnDoneFetchUrl done_callback) = 0;

  // Fetches the specified urls.
  //
  // requests: The URL and headers for the HTTP GET request.
  // timeout: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback may run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // Clients can expect done_callback to be called exactly once and are
  // guaranteed that the order of the reuslts exactly corresponds with the order
  // of requests.
  virtual void FetchUrls(const std::vector<HTTPRequest>& requests,
                         absl::Duration timeout,
                         OnDoneFetchUrls done_callback) = 0;

  // Fetches the specified urls with response metadata.
  //
  // requests: The URL and headers for the HTTP GET request.
  // timeout: The request timeout
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response. Please note that done_callback may run in a
  // threadpool and is not guaranteed to be the FetchUrl client's thread.
  // Clients can expect done_callback to be called exactly once and are
  // guaranteed that the order of the reuslts exactly corresponds with the order
  // of requests.
  virtual void FetchUrlsWithMetadata(
      const std::vector<HTTPRequest>& requests, absl::Duration timeout,
      OnDoneFetchUrlsWithMetadata done_callback) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_FETCHER_ASYNC_H_
