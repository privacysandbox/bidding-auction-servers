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

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"

namespace privacy_sandbox::bidding_auction_servers {

struct HTTPRequest {
  std::string url;
  // Optional
  std::vector<std::string> headers = {};
};

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
                        absl::AnyInvocable<void(absl::StatusOr<std::string>) &&>
                            done_callback) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_HTTP_FETCHER_ASYNC_H_
