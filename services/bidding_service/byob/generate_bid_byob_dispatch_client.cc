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

using ::privacy_sandbox::server_common::byob::UdfBlob;

absl::StatusOr<GenerateBidByobDispatchClient>
GenerateBidByobDispatchClient::Create(int num_workers,
                                      server_common::Executor* executor) {
  PS_ASSIGN_OR_RETURN(
      auto byob_service,
      roma_service::ByobGenerateProtectedAudienceBidService<>::Create({}));
  return GenerateBidByobDispatchClient(std::move(byob_service), num_workers,
                                       executor);
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
  PS_ASSIGN_OR_RETURN(
      std::string new_code_token,
      byob_service_.Register(udf_blob(), notif, load_status, num_workers_));
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

absl::Status GenerateBidByobDispatchClient::Execute(
    roma_service::GenerateProtectedAudienceBidRequest request,
    absl::Duration timeout,
    absl::AnyInvocable<
        void(absl::StatusOr<
             roma_service::GenerateProtectedAudienceBidResponse>) &&>
        callback) {
  auto response_state = std::make_unique<ResponseState>(std::move(callback));
  PS_VLOG(kNoisyInfo) << "Dispatching GenerateBid Binary UDF";
  // Start the call.
  PS_ASSIGN_OR_RETURN(
      auto execution_token,
      byob_service_.GenerateProtectedAudienceBid(
          [response_state_ptr = response_state.get()](
              absl::StatusOr<roma_service::GenerateProtectedAudienceBidResponse>
                  response) {
            absl::MutexLock execution_lock(
                &(response_state_ptr->execution_mutex));
            if (!response_state_ptr->is_cancelled) {
              std::move(response_state_ptr->callback)(std::move(response));
            }

            // Always notify to signal that response_state_ptr can be destroyed.
            response_state_ptr->execute_finish_notif.Notify();
          },
          std::move(request), /*metadata=*/{}, code_token_));

  // Start a thread to wait for response to become available, or cancel on
  // timeout
  PS_VLOG(kStats) << "Setting UDF Timeout: " << timeout;
  executor_->RunAfter(
      timeout, [byob_service = &byob_service_,
                execution_token = std::move(execution_token),
                response_state_ptr = std::move(response_state)]() mutable {
        {
          absl::MutexLock execution_lock(
              &(response_state_ptr->execution_mutex));
          if (response_state_ptr->execute_finish_notif.HasBeenNotified()) {
            PS_VLOG(8) << "UDF execution completed before timeout";
            return;
          }

          // Unblock reactor.
          std::move(response_state_ptr->callback)(absl::DeadlineExceededError(
              absl::StrCat("Deadline exceeded for generateBid execution: ",
                           execution_token.value)));
          response_state_ptr->is_cancelled = true;
          // Call cancel on ROMA API.
          PS_VLOG(kNoisyInfo) << "Cancelling generateBid UDF with token: "
                              << execution_token.value;
          byob_service->Cancel(execution_token);
        }

        // Wait for notification before destroying response_state_ptr.
        PS_VLOG(kNoisyInfo)
            << "Waiting for cancellation to invoke callback for: "
            << execution_token.value;
        response_state_ptr->execute_finish_notif.WaitForNotification();
        PS_VLOG(kNoisyInfo)
            << "Returning from timeout thread for cancelled request: "
            << execution_token.value;
        // After this line, response_state_ptr is invalid (null)
      });
  return absl::OkStatus();
}

GenerateBidByobDispatchClient::ResponseState::ResponseState(
    absl::AnyInvocable<
        void(absl::StatusOr<
             roma_service::GenerateProtectedAudienceBidResponse>) &&>
        callback)
    : is_cancelled(false), callback(std::move(callback)) {}
}  // namespace privacy_sandbox::bidding_auction_servers
