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

#include "services/common/clients/code_dispatcher/v8_dispatcher.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/blocking_counter.h"
#include "scp/cc/roma/interface/roma.h"

namespace privacy_sandbox::bidding_auction_servers {

using LoadRequest = ::google::scp::roma::CodeObject;
using LoadResponse = ::google::scp::roma::ResponseObject;
using LoadDoneCallback = ::google::scp::roma::Callback;

absl::Status V8Dispatcher::Init(DispatchConfig config) const {
  return google::scp::roma::RomaInit(config);
}

absl::Status V8Dispatcher::Stop() const {
  return google::scp::roma::RomaStop();
}

absl::Status V8Dispatcher::LoadSync(int version, absl::string_view js) const {
  LoadRequest request;
  request.version_num = version;
  request.js = js;
  absl::BlockingCounter is_loading(1);

  absl::Status load_status;
  absl::Status try_load = google::scp::roma::LoadCodeObj(
      std::make_unique<LoadRequest>(request),
      [&is_loading,
       &load_status](std::unique_ptr<absl::StatusOr<LoadResponse>> res) {
        if (!res->ok()) {
          load_status.Update(res->status());
        }
        is_loading.DecrementCount();
      });
  if (!try_load.ok()) {
    // Load callback won't be called, we can return.
    return try_load;
  } else {
    is_loading.Wait();
    return load_status;
  }
}

absl::Status V8Dispatcher::Execute(std::unique_ptr<DispatchRequest> request,
                                   DispatchDoneCallback done_callback) const {
  return google::scp::roma::Execute(std::move(request),
                                    std::move(done_callback));
}

absl::Status V8Dispatcher::BatchExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback batch_callback) const {
  return google::scp::roma::BatchExecute(batch, std::move(batch_callback));
}
}  // namespace privacy_sandbox::bidding_auction_servers
