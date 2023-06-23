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

#include "services/common/clients/code_dispatcher/code_dispatch_client.h"

#include <utility>

#include "glog/logging.h"

namespace privacy_sandbox::bidding_auction_servers {
absl::Status CodeDispatchClient::BatchExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback batch_callback) const {
  return dispatcher_.BatchExecute(batch, std::move(batch_callback));
}
}  // namespace privacy_sandbox::bidding_auction_servers
