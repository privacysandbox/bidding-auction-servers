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
#include "services/common/clients/async_grpc/request_config.h"
#include "src/logger/request_context_logger.h"

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
  // request - Request object for RPC. This will contain the request ciphertext.
  // callback - Final callback executed with the response when RPC is finished.
  explicit ClientParams(
      std::unique_ptr<Request> request,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<Response>>) &&>
          callback,
      const absl::flat_hash_map<std::string, std::string>& metadata = {}) {
    request_ = std::move(request);
    callback_ = std::move(callback);
    response_ = std::make_unique<Response>();

    for (const auto& it : metadata) {
      context_.AddMetadata(it.first, it.second);
    }
  }

  // GrpcClientParams is neither copyable nor movable.
  ClientParams(const ClientParams&) = delete;
  ClientParams& operator=(const ClientParams&) = delete;

  // Allows access to request param by gRPC
  Request* RequestRef() { return request_.get(); }

  // Allows access to context param by gRPC
  grpc::ClientContext* ContextRef() { return &context_; }

  // Allows access to response param by gRPC
  Response* ResponseRef() { return response_.get(); }

  // Sets a deadline for the gRPC request.
  void SetDeadline(absl::Duration timeout) {
    context_.set_deadline(
        std::chrono::system_clock::now() +
        std::chrono::milliseconds(ToInt64Milliseconds(timeout)));
  }

  // Deletes the current instance of ClientParams from the heap.
  // This should be invoked once in the callback after the gRPC request has
  // completed.
  void OnDone(const grpc::Status& status) {
    // callbacks can only run once
    if (status.ok()) {
      std::move(callback_)(std::move(response_));
    } else {
      std::move(callback_)(
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
      callback_ = nullptr;
};

template <class Request, class Response, class RawResponse>
class RawClientParams {
 public:
  // request - Request object for RPC. This will contain the request ciphertext.
  // callback - Final callback executed with the response when RPC is finished.
  explicit RawClientParams(
      std::unique_ptr<Request> request,
      absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>,
                              ResponseMetadata) &&>
          callback,
      RequestConfig request_config = {}) {
    request_ = std::move(request);
    raw_callback_ = std::move(callback);
    response_ = std::make_unique<Response>();
    request_config_ = request_config;
  }

  // GrpcClientParams is neither copyable nor movable.
  RawClientParams(const RawClientParams&) = delete;
  RawClientParams& operator=(const RawClientParams&) = delete;

  // Allows access to request config param by gRPC
  const RequestConfig RequestConfig() { return request_config_; }

  // Allows access to request param by gRPC
  Request* RequestRef() { return request_.get(); }

  // Allows access to response param by gRPC
  Response* ResponseRef() { return response_.get(); }

  void SetResponseMetadata(ResponseMetadata response_metadata) {
    response_metadata_ = response_metadata;
  }

  void OnDone(const grpc::Status& status) {
    if (status.ok()) {
      std::move(raw_callback_)(std::move(raw_response_), response_metadata_);
    } else {
      std::move(raw_callback_)(
          absl::Status(static_cast<absl::StatusCode>(status.error_code()),
                       status.error_message()),
          response_metadata_);
    }
    delete this;
  }

  void SetRawResponse(std::unique_ptr<RawResponse> raw_response) {
    raw_response_ = std::move(raw_response);
  }

 private:
  // Parameters will be accessed by the gRPC code.
  // Destructed automatically after OnDone
  std::unique_ptr<Request> request_;

  std::unique_ptr<Response> response_;
  // Ownership moves to callback in OnDone
  std::unique_ptr<RawResponse> raw_response_;

  // callback will only run once for a single gRPC call
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<RawResponse>>,
                          ResponseMetadata) &&>
      raw_callback_ = nullptr;

  struct RequestConfig request_config_;
  ResponseMetadata response_metadata_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // FLEDGE_SERVICES_COMMON_CLIENTS_CLIENT_PARAMS_H_
