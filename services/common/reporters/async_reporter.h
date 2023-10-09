//  Copyright 2023 Google LLC
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

#ifndef SERVICES_COMMON_ASYNC_REPORTER_H_
#define SERVICES_COMMON_ASYNC_REPORTER_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "services/common/clients/http/http_fetcher_async.h"

namespace privacy_sandbox::bidding_auction_servers {

// Provides functionality to perform asynchronous reporting.
class AsyncReporter {
 public:
  // Default constructor.
  explicit AsyncReporter(std::unique_ptr<HttpFetcherAsync> http_fetcher_async) {
    http_fetcher_async_ = std::move(http_fetcher_async);
  }

  virtual ~AsyncReporter() = default;

  // AsyncReporter is neither copyable nor movable.
  AsyncReporter(const AsyncReporter&) = delete;
  AsyncReporter& operator=(const AsyncReporter&) = delete;

  // Performs reporting by doing a get call on the URL.
  //
  // reporting_request: the request for reporting.
  // done_callback: Output param. Invoked either on error or after finished
  // receiving a response.
  virtual void DoReport(
      const HTTPRequest& reporting_request,
      absl::AnyInvocable<void(absl::StatusOr<absl::string_view>) &&>
          done_callback) const;

 private:
  std::unique_ptr<HttpFetcherAsync> http_fetcher_async_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_ASYNC_REPORTER_H_
