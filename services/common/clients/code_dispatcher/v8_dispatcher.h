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

#ifndef SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_V8_DISPATCHER_H_
#define SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_V8_DISPATCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "services/common/clients/code_dispatcher/udf_code_loader_interface.h"
#include "src/roma/interface/roma.h"
#include "src/roma/roma_service/roma_service.h"

namespace privacy_sandbox::bidding_auction_servers {

// The following aliases are part of the exported API so the
// user does not have to explicitly import the underlying library.
using DispatchRequest =
    google::scp::roma::InvocationSharedRequest<RomaRequestSharedContextBidding>;
using DispatchResponse = google::scp::roma::ResponseObject;
using DispatchDoneCallback = google::scp::roma::Callback;
using DispatchService = google::scp::roma::sandbox::roma_service::RomaService<
    RomaRequestSharedContextBidding>;
using BatchDispatchDoneCallback = google::scp::roma::BatchCallback;
// The DispatchConfig controls the number of worker processes, the number of
// threads per worker process, the IPC shared memory size (in bytes), and
// the max amount of tasks in the work queue before requests are rejected.
// Default values of {0, 0, 0, 0} allow the underlying library to choose
// these values as necessary.
using DispatchConfig = DispatchService::Config;

// This class is a wrapper around Roma, a library which provides an interface
// for multi-process javascript and wasm execution in V8.
class V8Dispatcher : public UdfCodeLoaderInterface {
 public:
  explicit V8Dispatcher(DispatchConfig&& config = DispatchConfig());

  virtual ~V8Dispatcher();

  // Initializes the dispatcher. Note that this call may bring up multiple
  // processes, which can be slow and should only happen on server startup.
  //
  // config: This represents all configurable params of the config. Please
  // pass in an empty struct '{}' to use default options (auto-scale).
  // return: a status indicated success or failure in starting. If startup
  // fails, a client may retry.
  absl::Status Init();

  // Loads new execution code synchronously. This is a blocking wrapper around
  // the google::scp::roma::LoadCodeObj method.
  //
  // version: the new version string of the code to load
  // code: the js code string to load
  // return: a status indicating whether the code load was successful.
  absl::Status LoadSync(std::string version, std::string code) override;

  // Executes a single request asynchronously.
  //
  // request: a unique pointer to the wrapper object containing all the
  // details necessary to execute the request.
  // done_callback: called with the output of the execution in a separate
  // thread managed by the underlying library.
  // return: a status indicating if the execution request was properly
  // scheduled. This should not be confused with the output of the execution
  // itself, which is sent to done_callback.
  virtual absl::Status Execute(std::unique_ptr<DispatchRequest> request,
                               DispatchDoneCallback done_callback);

  // Executes a batch of requests asynchronously. There are no guarantees
  // on the order of request processing.
  //
  // batch: a vector of requests, each executed independently and in parallel
  // batch_callback: called when all requests in the batch are finished.
  // return: a status indicating if the execution request was properly
  // scheduled. This should not be confused with the output of the execution
  // itself, which is sent to batch_callback.
  virtual absl::Status BatchExecute(std::vector<DispatchRequest>& batch,
                                    BatchDispatchDoneCallback batch_callback);

 private:
  DispatchService roma_service_;
};
}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CODE_DISPATCHER_V8_DISPATCHER_H_
