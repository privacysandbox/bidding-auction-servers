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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_ASYNC_CLIENT_H_

#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "services/common/clients/async_grpc/request_config.h"
#include "services/common/loggers/request_log_context.h"

namespace privacy_sandbox::bidding_auction_servers {

class CancellableAsyncClient {
 public:
  // Polymorphic class => virtual destructor
  virtual ~CancellableAsyncClient() = default;

  // Cancel the GRPC request
  virtual void Cancel() const {}
};

// This provides access to the Metadata Object type
using RequestMetadata = absl::flat_hash_map<std::string, std::string>;

// Classes implementing this template and interface are able to execute
// asynchronous requests.
template <typename Request, typename Response, typename RawRequest = Request,
          typename RawResponse = Response>
class AsyncClient {
 public:
  // Polymorphic class => virtual destructor
  virtual ~AsyncClient() = default;

  // Executes the request asynchronously.
  //
  // request: the request object to execute.
  // metadata: Metadata to be passed to the client.
  // on_done: callback called when the request is finished executing.
  // timeout: a timeout value for the request.
  virtual absl::Status Execute(
      std::unique_ptr<Request> request, const RequestMetadata& metadata,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Response>>) &&>
          on_done,
      absl::Duration timeout, RequestContext context = NoOpContext()) const {
    return absl::NotFoundError("Method not implemented.");
  }

  // Executes the inter service GRPC request asynchronously.
  virtual absl::Status ExecuteInternal(
      std::unique_ptr<RawRequest> request, grpc::ClientContext* context,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>,
                              ResponseMetadata) &&>
          on_done,
      absl::Duration timeout, RequestConfig request_config = {}) {
    return absl::NotFoundError("Method not implemented.");
  }
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_CLIENT_H_
