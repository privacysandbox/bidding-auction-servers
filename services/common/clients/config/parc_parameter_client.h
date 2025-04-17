/*
 * Copyright 2025 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_CLIENTS_CONFIG_PARC_PARAMETER_CLIENT_H_
#define SERVICES_COMMON_CLIENTS_CONFIG_PARC_PARAMETER_CLIENT_H_

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>

#include <apis/privacysandbox/apis/parc/v0/parc_service.grpc.pb.h>

#include "absl/status/status.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers {

class ParcParameterClient {
 public:
  explicit ParcParameterClient(
      std::unique_ptr<privacysandbox::apis::parc::v0::ParcService::Stub> stub)
      : stub_(std::move(stub)) {}

  virtual ~ParcParameterClient() = default;

  // Assembles the client's payload, sends it and returns the response.
  virtual absl::StatusOr<std::string> GetParameterSync(
      const std::string& parameter_name) {
    privacysandbox::apis::parc::v0::GetParameterRequest request;
    privacysandbox::apis::parc::v0::GetParameterResponse response;
    grpc::ClientContext context;
    request.set_parameter_name(parameter_name);
    if (grpc::Status status = stub_->GetParameter(&context, request, &response);
        !status.ok()) {
      return server_common::ToAbslStatus(status);
    }
    return response.parameter_value();
  }

 private:
  std::unique_ptr<privacysandbox::apis::parc::v0::ParcService::Stub> stub_;
};

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_CONFIG_PARC_PARAMETER_CLIENT_H_
