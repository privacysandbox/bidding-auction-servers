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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_PROVIDER_H_
#define SERVICES_COMMON_CLIENTS_ASYNC_PROVIDER_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {

// Used by the callback to pass the request and response byte sizes to the
// caller code of the Get method.
struct GetByteSize {
  size_t request;
  size_t response;
};

// Classes implementing this template and interface are able to execute
// asynchronous requests.
template <typename Params, typename Provision>
class AsyncProvider {
 public:
  // Polymorphic class => virtual destructor
  virtual ~AsyncProvider() = default;

  // Executes the provision asynchronously.
  //
  // params: the params required to provide the object.
  // on_done: callback called when the request is finished executing.
  // timeout: a timeout value for the request.
  virtual void Get(
      const Params& params,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Provision>>,
                              GetByteSize) &&>
          on_done,
      absl::Duration timeout, RequestContext context = NoOpContext()) const = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_PROVIDER_H_
