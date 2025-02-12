// Copyright 2023 Google LLC
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

#include "sandbox/sandbox_executor.h"

#include <fcntl.h>
#include <syscall.h>

#include <string>
#include <utility>

#include "absl/flags/flag.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "sandbox/sandbox_worker.h"
#include "sandboxed_api/sandbox2/allow_all_syscalls.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/runfiles.h"
#include "utils/resource_size_utils.h"

ABSL_FLAG(bool, testonly_disable_sandbox, false,
          "Disable sandbox restricted policies for testing purposes.");
ABSL_FLAG(bool, testonly_allow_policies_for_bazel, false,
          "Allow sandbox policies used in bazel for testing purposes.");

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// The same sandbox policy that Roma enforces.
// https://github.com/privacysandbox/data-plane-shared-libraries/blob/3db380a699ff33e359bfac60130b027948261a6a/src/roma/sandbox/worker_api/sapi/src/worker_sandbox_api.h#L182
void AllowSamePolicyAsRoma(sandbox2::PolicyBuilder& builder) {
  builder.AllowRead()
      .AllowWrite()
      .AllowOpen()
      .AllowSystemMalloc()
      .AllowHandleSignals()
      .AllowExit()
      .AllowStat()
      .AllowTime()
      .AllowGetIDs()
      .AllowGetPIDs()
      .AllowReadlink()
      .AllowMmap()
      .AllowFork()
#ifdef UNDEFINED_BEHAVIOR_SANITIZER
      .AllowPipe()
      .AllowLlvmSanitizers()
#endif
      .AllowSyscall(__NR_tgkill)
      .AllowSyscall(__NR_recvmsg)
      .AllowSyscall(__NR_sendmsg)
      .AllowSyscall(__NR_lseek)
      .AllowSyscall(__NR_futex)
      .AllowSyscall(__NR_close)
      .AllowSyscall(__NR_nanosleep)
      .AllowSyscall(__NR_sched_getaffinity)
      .AllowSyscall(__NR_mprotect)
      .AllowSyscall(__NR_clone3)
      .AllowSyscall(__NR_rseq)
      .AllowSyscall(__NR_set_robust_list)
      .AllowSyscall(__NR_prctl)
      .AllowSyscall(__NR_uname)
      .AllowSyscall(__NR_pkey_alloc)
      .AllowSyscall(__NR_madvise)
      .AllowSyscall(__NR_ioctl)
      .AllowSyscall(__NR_prlimit64)
      //------------------------
      // These are for TCMalloc:
      .AllowTcMalloc()
      .AllowSyscall(__NR_sched_getparam)
      .AllowSyscall(__NR_sched_getscheduler)
      .AllowSyscall(__NR_clock_nanosleep)
      .AllowSyscall(__NR_sched_yield)
      .AllowSyscall(__NR_rseq)
      //------------------------
      .AllowDynamicStartup()
      .DisableNamespaces()
      .CollectStacktracesOnViolation(false)
      .CollectStacktracesOnSignal(false)
      .CollectStacktracesOnTimeout(false)
      .CollectStacktracesOnKill(false)
      .CollectStacktracesOnExit(false);

// Stack traces are only collected in DEBUG mode.
#ifndef NDEBUG
  builder.CollectStacktracesOnViolation(true)
      .CollectStacktracesOnSignal(true)
      .CollectStacktracesOnTimeout(true)
      .CollectStacktracesOnKill(true)
      .CollectStacktracesOnExit(true);
#endif
}

void AllowBazelTest(sandbox2::PolicyBuilder& builder) {
  // For `bazel coverage`.
  builder.AllowMkdir().AllowSyscall(__NR_ftruncate);
}

void AllowGrpc(sandbox2::PolicyBuilder& builder) {
  builder.AllowSyscall(__NR_eventfd2)
      .AllowEpoll()
      .AllowSyscall(__NR_getsockname)
      .AddPolicyOnSyscall(__NR_setsockopt,
                          {
                              ARG(1) /*level*/,
                              JNE(IPPROTO_TCP, DENY),
                              ARG(2), /*option_name*/
                              JEQ(36 /*TCP_INQ*/, ALLOW),
                          })

      .AllowSafeFcntl()
      .AllowSyscall(__NR_getrandom)
      .AllowSyscalls({__NR_recvmsg, __NR_sendmsg})
      .AddPolicyOnSyscall(__NR_tgkill,
                          {ARG_32(2) /*sig*/, JEQ32(SIGABRT, ALLOW)});
}

void AllowAws(sandbox2::PolicyBuilder& builder) {
  // TODO(b/333559278): Revisit the need for the following permissions.
  builder.AllowSyscall(__NR_getsockopt);
}

void AllowPyTorch(sandbox2::PolicyBuilder& builder) {
  // TODO(b/323601668): Rewrite the PyTorch sidecar in order not to call
  // sysinfo.
  builder.AllowSyscall(__NR_sysinfo);
}

void AllowTensorFlow(sandbox2::PolicyBuilder& builder) {
  // TODO(b/316960066): Do not read directories in the image.
  builder.AllowReaddir();
}

void AllowAmx(sandbox2::PolicyBuilder& builder) {
  // arch_prctl is required for Intel AMX.
  builder.AllowSyscall(__NR_arch_prctl);
}

std::unique_ptr<sandbox2::Policy> MakePolicy() {
  if (absl::GetFlag(FLAGS_testonly_disable_sandbox)) {
    return sandbox2::PolicyBuilder()
        .DisableNamespaces()
        .DefaultAction(sandbox2::AllowAllSyscalls())
        .BuildOrDie();
  }

  sandbox2::PolicyBuilder builder = sandbox2::PolicyBuilder();
  if (absl::GetFlag(FLAGS_testonly_allow_policies_for_bazel)) {
    AllowBazelTest(builder);
  }
  AllowSamePolicyAsRoma(builder);
  AllowGrpc(builder);
  AllowAws(builder);
  AllowPyTorch(builder);
  AllowTensorFlow(builder);
  AllowAmx(builder);
  builder.AllowStaticStartup().AllowLogForwarding();

  return builder.BuildOrDie();
}

}  // namespace

std::string SandboxeeStateToString(SandboxeeState sandboxee_state) {
  switch (sandboxee_state) {
    case SandboxeeState::kNotStarted:
      return "NotStarted";
    case SandboxeeState::kRunning:
      return "Running";
    case SandboxeeState::kStopped:
      return "Stopped";
  }

  int sandboxee_state_raw = static_cast<int>(sandboxee_state);
  ABSL_LOG(ERROR) << "Unexpected sandboxee_state_ value: "
                  << sandboxee_state_raw;
  return absl::StrCat(sandboxee_state_raw);
}

std::string GetFilePath(absl::string_view relative_binary_path) {
  return sapi::GetDataDependencyFilePath(relative_binary_path);
}

SandboxExecutor::SandboxExecutor(absl::string_view binary_path,
                                 const std::vector<std::string>& args,
                                 const int64_t rlimit_mb) {
  auto executor = std::make_unique<sandbox2::Executor>(binary_path, args);
  executor->limits()
      ->set_rlimit_cpu(RLIM64_INFINITY)
      .set_rlimit_as(rlimit_mb == 0 ? RLIM64_INFINITY
                                    : GetByteSizeFromMb(rlimit_mb))
      .set_rlimit_core(0)
      .set_walltime_limit(absl::InfiniteDuration());
  executor->ipc()->EnableLogServer();

  // The sandboxee should call `SandboxMeHere()` manually to start applying
  // syscall policy.
  executor->set_enable_sandbox_before_exec(false);

  // The executor receives a file descriptor of the sandboxee FD.
  file_descriptor_ = executor->ipc()->ReceiveFd(kFileDescriptorName);
  sandbox_ =
      std::make_unique<sandbox2::Sandbox2>(std::move(executor), MakePolicy());
}

SandboxExecutor::~SandboxExecutor() { StopSandboxee().IgnoreError(); }

absl::Status SandboxExecutor::StartSandboxee() {
  if (sandboxee_state_ != SandboxeeState::kNotStarted) {
    return absl::FailedPreconditionError(
        absl::StrCat("Sandboxee cannot start. Current state: ",
                     SandboxeeStateToString(sandboxee_state_)));
  }

  ABSL_LOG(INFO) << "SandboxExecutor: Starting sandboxee";
  if (!sandbox_->RunAsync()) {
    sandboxee_state_ = SandboxeeState::kStopped;
    run_result_ = sandbox_->AwaitResult();
    ABSL_LOG(ERROR) << "SandboxExecutor: Failed to start: "
                    << run_result_.ToString();
    return run_result_.ToStatus();
  }
  sandboxee_state_ = SandboxeeState::kRunning;
  ABSL_LOG(INFO) << "SandboxExecutor: Started sandboxee";
  return absl::OkStatus();
}

pid_t SandboxExecutor::GetPid() const {
  if (!sandbox_) {
    ABSL_LOG(ERROR) << "Sandbox object is null. Cannot retrieve PID.";
    return -1;
  }

  pid_t pid = sandbox_->pid();
  if (pid <= 0) {
    ABSL_LOG(ERROR) << "Failed to obtain PID for the inference sidecar. PID: "
                    << pid;
    return -1;
  }
  return pid;
}

absl::StatusOr<sandbox2::Result> SandboxExecutor::StopSandboxee() {
  if (sandboxee_state_ == SandboxeeState::kStopped) {
    return run_result_;
  }

  if (sandboxee_state_ == SandboxeeState::kNotStarted) {
    ABSL_LOG(ERROR) << "Sandboxee wasn't started when StopSandboxee is called";
    sandboxee_state_ = SandboxeeState::kStopped;
    return absl::FailedPreconditionError(
        "Sandboxee wasn't started when StopSandboxee is called");
  }

  sandboxee_state_ = SandboxeeState::kStopped;

  ABSL_LOG(INFO)
      << "SandboxExecutor: Checking if sandboxee stopped on their own";
  absl::StatusOr<sandbox2::Result> self_stopped_result =
      sandbox_->AwaitResultWithTimeout(absl::ZeroDuration());
  if (self_stopped_result.ok()) {
    ABSL_LOG(INFO) << "SandboxExecutor: Sandboxee stopped on their own";
    run_result_ = self_stopped_result.value();
    return run_result_;
  }

  // sandbox_->Kill() can be called only if sandboxee had been started before
  // and setup didn't fail
  ABSL_LOG(INFO) << "SandboxExecutor: Sending SIGKILL to sandboxee";
  sandbox_->Kill();
  absl::StatusOr<sandbox2::Result> kill_result =
      sandbox_->AwaitResultWithTimeout(absl::Minutes(1));
  // Note: SIGKILL can be delayed.
  if (kill_result.ok()) {
    ABSL_LOG(INFO) << "SandboxExecutor: Killed sandboxee";
    run_result_ = kill_result.value();
    return run_result_;
  } else {
    ABSL_LOG(ERROR)
        << "SandboxExecutor: Failed to send SIGKILL or to wait until "
           "sandboxee killed. Continuing with stopping";
    return kill_result;
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
