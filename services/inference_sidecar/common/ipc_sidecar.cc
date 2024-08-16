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

// Runs a simple IPC single-threaded processor.

#include "ipc_sidecar.h"

#include <memory>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_worker.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using ::sandbox2::Comms;

// Multiple receive trials to handle IPC non-blocking receive failure.
constexpr int kRecvNumTrials = 10;

}  // namespace

absl::Status Run(const InferenceSidecarRuntimeConfig& config) {
  SandboxWorker worker;
  sandbox2::Comms comms(worker.FileDescriptor());

  if (!config.module_name().empty() &&
      config.module_name() != ModuleInterface::GetModuleVersion()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Expected inference module: ", config.module_name(),
                     ", but got : ", ModuleInterface::GetModuleVersion()));
  }
  std::unique_ptr<ModuleInterface> inference_module =
      ModuleInterface::Create(config);

  // TODO(b/322109220): Handles arbitrary request proto simultaneously.
  // TODO(b/325123788): Remove retry logic with gRPC over IPC.
  RegisterModelRequest register_request;
  bool register_request_received = false;
  for (int i = 0; i < kRecvNumTrials; i++) {
    register_request_received = comms.RecvProtoBuf(&register_request);
    if (register_request_received) break;
    ABSL_LOG(INFO) << "Recv RegisterModelRequest failure #" << i + 1;
    absl::SleepFor(absl::Milliseconds(100));
  }
  if (!register_request_received ||
      !inference_module->RegisterModel(register_request).ok()) {
    return absl::FailedPreconditionError("Failure to register model.");
  }

  RegisterModelResponse register_response;
  ABSL_LOG_IF(ERROR, !comms.SendProtoBuf(register_response))
      << "Failure to send RegisterModelResponse";

  while (true) {
    PredictRequest predict_request;
    bool predict_request_received = false;
    // TODO(b/325123788): Remove retry logic with gRPC over IPC.
    // Retry to mitigate "Resource temporary not available" errors.
    for (int i = 0; i < kRecvNumTrials; i++) {
      predict_request_received = comms.RecvProtoBuf(&predict_request);
      if (predict_request_received) break;
      ABSL_LOG(INFO) << "Recv PredictRequest failure #" << i + 1;
      absl::SleepFor(absl::Microseconds(500));
    }

    if (!predict_request_received) {
      ABSL_LOG(INFO) << "No PredictRequest received.";
    } else {
      // Stops the sidecar if any inference request fails for now.
      PS_ASSIGN_OR_RETURN(PredictResponse inference_result,
                          inference_module->Predict(predict_request));
      ABSL_LOG_IF(ERROR, !comms.SendProtoBuf(inference_result))
          << "Failure to send PredictResponse";
    }
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
