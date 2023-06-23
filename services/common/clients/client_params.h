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

#ifndef FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_PARAMS_H_
#define FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_PARAMS_H_

#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

namespace privacy_sandbox::bidding_auction_servers {

// This class handles the lifecycle of parameters on the heap required for
// the entire timeline of a single gRPC request.
// Usage:
// auto* params = new ClientParams<I,O>(input, callback);
// stub->async()->RPC(
//      params->ContextRef(), params->RequestRef(),
//      params->ResponseRef(), [params](const grpc::Status& status){
//        // cannot be called again
//        params->OnDone(status);
//      });
template <class Request, class Response>
class ClientParams {
 public:
  // request - Request object for RPC
  // callback - Final callback executed with the response when RPC is finished
  explicit ClientParams(
      std::unique_ptr<Request> request,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Response>>) &&>
          callback,
      const absl::flat_hash_map<std::string, std::string>& metadata = {}) {
    this->request_ = std::move(request);
    this->callback_ = std::move(callback);
    this->response_ = std::make_unique<Response>();
    for (const auto& it : metadata) {
      this->context_.AddMetadata(it.first, it.second);
    }
  }

  // GrpcClientParams is neither copyable nor movable.
  ClientParams(const ClientParams&) = delete;
  ClientParams& operator=(const ClientParams&) = delete;

  // Allows access to request param by gRPC
  Request* RequestRef() { return this->request_.get(); }

  // Allows access to context param by gRPC
  grpc::ClientContext* ContextRef() { return &this->context_; }

  // Allows access to response param by gRPC
  Response* ResponseRef() { return this->response_.get(); }

  // Sets a deadline for the gRPC request.
  void SetDeadline(absl::Duration timeout) {
    this->context_.set_deadline(
        std::chrono::system_clock::now() +
        std::chrono::milliseconds(ToInt64Milliseconds(timeout)));
  }

  // Deletes the current instance of ClientParams from the heap.
  // This should be invoked once in the callback after the gRPC request has
  // completed.
  void OnDone(const grpc::Status& status) {
    // callbacks can only run once
    if (status.ok()) {
      std::move(this->callback_)(std::move(response_));
    } else {
      std::move(this->callback_)(
          absl::Status(static_cast<absl::StatusCode>(status.error_code()),
                       status.error_message()));
    }
    delete this;
  }

 private:
  // Parameters will be accessed by the gRPC code.
  // Destructed automatically after OnDone
  grpc::ClientContext context_;
  std::unique_ptr<Request> request_;

  // Ownership moves to callback in OnDone
  std::unique_ptr<Response> response_;

  // callback will only run once for a single gRPC call
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Response>>) &&>
      callback_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_PARAMS_H_
