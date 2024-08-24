/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_DATA_FETCH_DATA_FETCHER_INTERFACE_H_
#define SERVICES_COMMON_DATA_FETCH_DATA_FETCHER_INTERFACE_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"

namespace privacy_sandbox::bidding_auction_servers {

using WrapCodeForDispatch =
    absl::AnyInvocable<std::string(const std::vector<std::string>&)>;

using WrapSingleCodeBlobForDispatch =
    absl::AnyInvocable<std::string(const std::string&)>;

// Interface for different versions of data fetchers.
class FetcherInterface {
 public:
  virtual ~FetcherInterface() = default;

  // Starts a new data blob fetching process.
  virtual absl::Status Start() = 0;

  // Ends the data blob fetching process by canceling scheduled tasks. Should be
  // called when there is no more need to update generateBid.js and scoreAd.js,
  // and also in the deconstructor.
  virtual void End() = 0;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_DATA_FETCH_DATA_FETCHER_INTERFACE_H_
