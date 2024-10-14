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

#ifndef SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_V8_DISPATCH_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_V8_DISPATCH_CLIENT_H_

#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "services/common/clients/code_dispatcher/v8_dispatcher.h"
#include "src/roma/interface/roma.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class acts as a client for dispatching javascript + wasm to be
// executed in a different process sandbox.
class V8DispatchClient {
 public:
  explicit V8DispatchClient(V8Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {}

  // Required to create this on the heap.
  virtual ~V8DispatchClient() = default;

  // Execute a batch of requests asynchronously via the code dispatcher library.
  // There are no guarantees on request order processing.
  //
  // batch: a vector of requests, each executed independently and in parallel
  // batch_callback: called when all requests in the batch are finished.
  // return: a status indicating if the execution request was properly
  // scheduled. This should not be confused with the output of the execution
  // itself, which is sent to batch_callback.
  virtual absl::Status BatchExecute(std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback batch_callback);

 private:
  V8Dispatcher& dispatcher_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_V8_DISPATCH_CLIENT_H_
