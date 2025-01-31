//  Copyright 2023 Google LLC
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

#ifndef SANDBOX_SANDBOX_EXECUTOR_H_
#define SANDBOX_SANDBOX_EXECUTOR_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/sandbox2.h"

ABSL_DECLARE_FLAG(bool, testonly_disable_sandbox);
ABSL_DECLARE_FLAG(bool, testonly_allow_policies_for_bazel);

namespace privacy_sandbox::bidding_auction_servers::inference {

// State of the sandboxee.
enum class SandboxeeState {
  // The initial state before sandboxee is started.
  kNotStarted = 0,
  // The state to indicate that sandboxee has started successfully.
  kRunning,
  // The final state after sandboxee has been stopped or killed, cannot be
  // switched to any other state.
  kStopped,
};

// Transforms sandboxee state to string for logging purposes.
std::string SandboxeeStateToString(SandboxeeState sandboxee_state);

// Returns the full path of the sandboxee binary added as the data of the
// executor binary.
std::string GetFilePath(absl::string_view relative_binary_path);

// SandboxExecutor runs a given binary as a child process under Sandbox2.
// It also sets up the IPC communication with `SandboxWorker`.
// Not thread safe.
class SandboxExecutor {
 public:
  SandboxExecutor(absl::string_view binary_path,
                  const std::vector<std::string>& args,
                  const int64_t rlimit_mb = 0);
  ~SandboxExecutor();

  SandboxExecutor(const SandboxExecutor&) = delete;
  SandboxExecutor& operator=(const SandboxExecutor&) = delete;

  // Starts a sandboxee instance asynchronously.
  // You should not call more than once.
  // You should not call after `StopSandboxee`.
  absl::Status StartSandboxee();

  // Get the pid of inference sidecar
  pid_t GetPid() const;

  // Stops the sandboxee instance. Returns an error status if the sandboxee
  // fails to stop. Returns `sandbox2::Result` if stopped successfully. You can
  // call many times safely. You should not call other operations like
  // `StartSandboxee` after this call.
  absl::StatusOr<sandbox2::Result> StopSandboxee();

  // Returns opened file descriptor to communicate with the sandboxee via IPC.
  int FileDescriptor() const { return file_descriptor_; }

 private:
  std::unique_ptr<sandbox2::Sandbox2> sandbox_;
  // File descriptor used for IPC.
  int file_descriptor_;
  SandboxeeState sandboxee_state_ = SandboxeeState::kNotStarted;
  // Populated on the first successful attempt of the sandboxee stop.
  sandbox2::Result run_result_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SANDBOX_SANDBOX_EXECUTOR_H_
