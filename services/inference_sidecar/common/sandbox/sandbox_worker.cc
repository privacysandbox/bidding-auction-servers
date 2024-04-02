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

#include "sandbox/sandbox_worker.h"

#include <fcntl.h>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Makes `file_descriptor` non-blocking to allow communication via gRPC.
absl::Status MakeFileDescriptorNonBlocking(int file_descriptor) {
  int flags = fcntl(file_descriptor, F_GETFL, 0);
  if (fcntl(file_descriptor, F_SETFL, flags | O_NONBLOCK) == -1) {
    return absl::InternalError("Failed to set `O_NONBLOCK`");
  }
  return absl::OkStatus();
}

}  // namespace

SandboxWorker::SandboxWorker()
    : comms_(sandbox2::Comms::kSandbox2ClientCommsFD),
      sandbox2_client_(&comms_) {
  // Enable sandboxing from here. Getting FDs should happen after the call.
  sandbox2_client_.SandboxMeHere();
  sandbox2_client_.SendLogsToSupervisor();

  file_descriptor_ = sandbox2_client_.GetMappedFD(kFileDescriptorName);
  CHECK_OK(MakeFileDescriptorNonBlocking(file_descriptor_))
      << "Unable to make fd=" << file_descriptor_
      << " non-blocking. It prevents communication via gRPC";
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
