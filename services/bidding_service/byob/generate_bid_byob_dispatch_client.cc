// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/byob/generate_bid_byob_dispatch_client.h"

#include <functional>
#include <memory>

#include "absl/synchronization/notification.h"
#include "services/common/loggers/request_log_context.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::privacy_sandbox::server_common::byob::Mode;
using ::privacy_sandbox::server_common::byob::UdfBlob;

absl::StatusOr<GenerateBidByobDispatchClient>
GenerateBidByobDispatchClient::Create(int num_workers) {
  PS_ASSIGN_OR_RETURN(
      auto byob_service,
      roma_service::ByobGenerateProtectedAudienceBidService<>::Create(
          {
              .lib_mounts = "",
              .enable_seccomp_filter = true,
          },
          /*mode=*/Mode::kModeNsJailSandbox));
  return GenerateBidByobDispatchClient(std::move(byob_service), num_workers);
}

absl::Status GenerateBidByobDispatchClient::LoadSync(std::string version,
                                                     std::string code) {
  if (version.empty()) {
    return absl::InvalidArgumentError(
        "Code version to be loaded cannot be empty.");
  }
  if (code.empty()) {
    return absl::InvalidArgumentError("Code to be loaded cannot be empty.");
  }

  // Skip if code hash matches the hash of already loaded code.
  std::size_t new_code_hash = std::hash<std::string>{}(code);
  if (new_code_hash == code_hash_) {
    return absl::OkStatus();
  }

  // Get UDF blob for the given code and register it with the BYOB service.
  PS_ASSIGN_OR_RETURN(UdfBlob udf_blob, UdfBlob::Create(std::move(code)));
  PS_ASSIGN_OR_RETURN(std::string new_code_token,
                      byob_service_.Register(udf_blob(), num_workers_));

  // Acquire lock before updating info about the most recently loaded code
  // blob.
  if (code_mutex_.TryLock()) {
    code_token_ = std::move(new_code_token);
    code_version_ = std::move(version);
    code_hash_ = new_code_hash;
    code_mutex_.Unlock();
  }
  return absl::OkStatus();
}

absl::Status GenerateBidByobDispatchClient::Execute(
    const roma_service::GenerateProtectedAudienceBidRequest& request,
    absl::Duration timeout,
    absl::AnyInvocable<
        void(absl::StatusOr<
             roma_service::GenerateProtectedAudienceBidResponse>) &&>
        callback) {
  PS_VLOG(kNoisyInfo) << "Dispatching GenerateBid Binary UDF";
  // Start the call.
  return byob_service_
      .GenerateProtectedAudienceBid(
          [callback = std::move(callback)](
              absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>
                  response) mutable {
            std::move(callback)(std::move(response));
          },
          request, /*metadata=*/{}, code_token_)
      .status();
}

}  // namespace privacy_sandbox::bidding_auction_servers
