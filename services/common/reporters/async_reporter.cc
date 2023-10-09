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

#include "services/common/reporters/async_reporter.h"

namespace privacy_sandbox::bidding_auction_servers {

constexpr int kNormalTimeoutMs = 5000;

void AsyncReporter::DoReport(
    const HTTPRequest& reporting_request,
    absl::AnyInvocable<void(absl::StatusOr<absl::string_view>) &&>
        done_callback) const {
  http_fetcher_async_->FetchUrl(reporting_request, kNormalTimeoutMs,
                                std::move(done_callback));
}
}  // namespace privacy_sandbox::bidding_auction_servers
