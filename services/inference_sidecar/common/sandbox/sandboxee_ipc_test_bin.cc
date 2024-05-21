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

// Sandboxee binary to test IPC via `sandbox2::Comms`.

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "proto/testproto.pb.h"
#include "sandbox/sandbox_worker.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using ::sandbox2::Comms;

void Run() {
  SandboxWorker worker;
  Comms comms(worker.FileDescriptor());

  // Retry to mitigate "Resource temporary not available" errors.
  Empty request;
  bool request_received = false;
  const int attempts_count = 5;
  for (int i = 0; i < attempts_count; i++) {
    request_received = comms.RecvProtoBuf(&request);
    if (request_received) break;
    ABSL_LOG(INFO) << i + 1 << "th failure";
  }
  CHECK(request_received);
  Empty response;
  CHECK(comms.SendProtoBuf(response));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference

int main(int argc, char** argv) {
  privacy_sandbox::bidding_auction_servers::inference::Run();
  return 0;
}
