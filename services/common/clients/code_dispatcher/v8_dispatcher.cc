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
#include "absl/synchronization/notification.h"
#include "services/common/loggers/request_log_context.h"
#include "src/roma/interface/roma.h"

namespace privacy_sandbox::bidding_auction_servers {

using LoadRequest = ::google::scp::roma::CodeObject;
using LoadResponse = ::google::scp::roma::ResponseObject;
using LoadDoneCallback = ::google::scp::roma::Callback;

V8Dispatcher::V8Dispatcher(DispatchConfig&& config)
    : roma_service_(std::move(config)) {}

V8Dispatcher::~V8Dispatcher() {
  PS_LOG(ERROR, SystemLogContext()) << "Stopping roma service...";
  absl::Status stop_status = roma_service_.Stop();
  PS_LOG(ERROR, SystemLogContext())
      << "Roma service stop status: " << stop_status;
}

absl::Status V8Dispatcher::Init() { return roma_service_.Init(); }

absl::Status V8Dispatcher::LoadSync(std::string version, std::string code) {
  auto request = std::make_unique<LoadRequest>(LoadRequest{
      .version_string = std::move(version),
      .js = std::move(code),
  });
  absl::Notification load_finished;
  absl::Status load_status;
  if (absl::Status try_load = roma_service_.LoadCodeObj(
          std::move(request),
          [&load_finished,
           &load_status](absl::StatusOr<LoadResponse> res) {  // NOLINT
            if (!res.ok()) {
              load_status.Update(res.status());
            }
            load_finished.Notify();
          });
      !try_load.ok()) {
    // Load callback won't be called, we can return.
    return try_load;
  }
  load_finished.WaitForNotification();
  return load_status;
}

absl::Status V8Dispatcher::Execute(std::unique_ptr<DispatchRequest> request,
                                   DispatchDoneCallback done_callback) {
  return roma_service_.Execute(std::move(request), std::move(done_callback))
      .status();
}

absl::Status V8Dispatcher::BatchExecute(
    std::vector<DispatchRequest>& batch,
    BatchDispatchDoneCallback batch_callback) {
  return roma_service_.BatchExecute(batch, std::move(batch_callback));
}
}  // namespace privacy_sandbox::bidding_auction_servers
