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
  const size_t batch_size = batch.size();
  auto batch_response =
      std::make_shared<std::vector<absl::StatusOr<DispatchResponse>>>(
          batch_size, absl::StatusOr<DispatchResponse>());
  auto finished_counter = std::make_shared<std::atomic<size_t>>(0);
  auto batch_callback_ptr =
      std::make_shared<BatchDispatchDoneCallback>(std::move(batch_callback));

  for (size_t index = 0; index < batch_size; ++index) {
    auto single_callback =
        [batch_response, finished_counter, batch_callback_ptr,
         index](absl::StatusOr<DispatchResponse> obj_response) {
          (*batch_response)[index] = std::move(obj_response);
          auto finished_value = finished_counter->fetch_add(1);
          if (finished_value + 1 == batch_response->size()) {
            (*batch_callback_ptr)(std::move(*batch_response));
          }
        };

    absl::Status result;
    while (
        !(result = roma_service_
                       .Execute(std::make_unique<DispatchRequest>(batch[index]),
                                single_callback)
                       .status())
             .ok()) {
      // If the first request from the batch got a failure, return failure
      // without waiting.
      if (index == 0) {
        return result;
      }
    }
  }

  return absl::OkStatus();
}
}  // namespace privacy_sandbox::bidding_auction_servers
