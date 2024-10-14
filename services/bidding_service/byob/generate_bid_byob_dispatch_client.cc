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
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers {

using ::privacy_sandbox::server_common::byob::UdfBlob;

absl::StatusOr<GenerateBidByobDispatchClient>
GenerateBidByobDispatchClient::Create(int num_workers) {
  PS_ASSIGN_OR_RETURN(
      auto byob_service,
      roma_service::ByobGenerateProtectedAudienceBidService<>::Create(
          {.num_workers = num_workers}));
  return GenerateBidByobDispatchClient(std::move(byob_service));
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
  absl::Notification notif;
  absl::Status load_status;
  PS_ASSIGN_OR_RETURN(std::string new_code_token,
                      byob_service_.Register(udf_blob(), notif, load_status));
  // TODO(b/368624844): Make duration configurable by taking in this in Create.
  notif.WaitForNotificationWithTimeout(absl::Seconds(120));

  if (load_status.ok()) {
    // Acquire lock before updating info about the most recently loaded code
    // blob.
    if (code_mutex_.TryLock()) {
      code_token_ = std::move(new_code_token);
      code_version_ = std::move(version);
      code_hash_ = new_code_hash;
      code_mutex_.Unlock();
    }
  }
  return load_status;
}

absl::StatusOr<
    std::unique_ptr<roma_service::GenerateProtectedAudienceBidResponse>>
GenerateBidByobDispatchClient::Execute(
    const roma_service::GenerateProtectedAudienceBidRequest& request,
    absl::Duration timeout) {
  absl::StatusOr<
      std::unique_ptr<roma_service::GenerateProtectedAudienceBidResponse>>
      response;
  absl::Notification notif;
  PS_ASSIGN_OR_RETURN(
      auto execution_token,
      byob_service_.GenerateProtectedAudienceBid(notif, request, response,
                                                 /*metadata=*/{}, code_token_));
  notif.WaitForNotificationWithTimeout(timeout);
  if (!response.ok() || response.value() != nullptr) {
    return response;
  }
  return absl::DeadlineExceededError(
      "Deadline exceeded while running generateBid.");
}
}  // namespace privacy_sandbox::bidding_auction_servers
