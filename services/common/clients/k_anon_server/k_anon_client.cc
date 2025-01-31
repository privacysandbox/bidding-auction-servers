//  Copyright 2024 Google LLC
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

#include "services/common/clients/k_anon_server/k_anon_client.h"

#include <algorithm>

#include "absl/strings/str_join.h"
#include "services/common/clients/async_grpc/default_async_grpc_client.h"
#include "services/common/clients/client_params.h"

namespace privacy_sandbox::bidding_auction_servers {

KAnonGrpcClient::KAnonGrpcClient(const KAnonClientConfig& client_config) {
  metadata_ = {{"x-goog-api-key", client_config.api_key}};
  stub_ = KAnonymousSetsQueryService::NewStub(CreateChannel(
      client_config.server_addr, client_config.compression,
      client_config.secure_client, /*grpc_arg_default_authority=*/"",
      client_config.ca_root_pem));
}

absl::Status KAnonGrpcClient::Execute(
    std::unique_ptr<ValidateHashesRequest> request,
    absl::AnyInvocable<
        void(absl::StatusOr<std::unique_ptr<ValidateHashesResponse>>) &&>
        on_done,
    absl::Duration timeout) {
  PS_VLOG(6) << "ValidateHashesRequest: " << request->DebugString()
             << " with metadata: "
             << absl::StrJoin(metadata_, " | ", absl::PairFormatter("="));
  auto params = std::make_unique<
      ClientParams<ValidateHashesRequest, ValidateHashesResponse>>(
      std::move(request), std::move(on_done), metadata_);
  params->SetDeadline(std::min(k_anon_client_max_timeout, timeout));
  {
    absl::MutexLock lock(&active_calls_mutex_);
    ++active_calls_count_;
  }
  stub_->async()->ValidateHashes(
      params->ContextRef(), params->RequestRef(), params->ResponseRef(),
      [this, params_ptr = params.release()](const grpc::Status& status) {
        PS_VLOG(6) << "ValidateHashesResponse: "
                   << params_ptr->ResponseRef()->DebugString();
        params_ptr->OnDone(status);
        {
          absl::MutexLock lock(&active_calls_mutex_);
          --active_calls_count_;
        }
      });
  return absl::OkStatus();
}

KAnonGrpcClient::~KAnonGrpcClient() {
  absl::MutexLock lock(
      &active_calls_mutex_,
      absl::Condition(
          +[](int* cnt) { return *cnt == 0; }, &active_calls_count_));
}

}  // namespace privacy_sandbox::bidding_auction_servers
