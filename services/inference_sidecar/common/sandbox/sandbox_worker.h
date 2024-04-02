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

#ifndef SANDBOX_SANDBOX_WORKER_H_
#define SANDBOX_SANDBOX_WORKER_H_

#include <utility>

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Name of the `file_descriptor` to establish IPC connection.
// A host sends a request to a sandboxee via this `file_descriptor`.
inline constexpr char kFileDescriptorName[] = "CommFD";

// Sandbox worker.
// It's attached to the sandboxee, and provides communication channel between
// the host and the sandboxee.
class SandboxWorker {
 public:
  SandboxWorker();
  SandboxWorker(const SandboxWorker&) = delete;
  SandboxWorker& operator=(const SandboxWorker&) = delete;

  int FileDescriptor() { return file_descriptor_; }

 private:
  sandbox2::Comms comms_;
  sandbox2::Client sandbox2_client_;
  // File descriptor used for IPC.
  int file_descriptor_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SANDBOX_SANDBOX_WORKER_H_
