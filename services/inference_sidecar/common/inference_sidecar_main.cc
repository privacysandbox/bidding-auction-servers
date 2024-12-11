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

// Inference sidecar binary.

#include <google/protobuf/util/json_util.h>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "proto/inference_sidecar.pb.h"

#include "grpc_sidecar.h"

int main(int argc, char** argv) {
  privacy_sandbox::bidding_auction_servers::inference::
      InferenceSidecarRuntimeConfig config;
  CHECK(google::protobuf::util::JsonStringToMessage(argv[0], &config).ok())
      << "Could not parse inference sidecar runtime config JsonString to a "
         "proto message.";
  CHECK(privacy_sandbox::bidding_auction_servers::inference::SetCpuAffinity(
            config)
            .ok())
      << "Could not set CPU affinity.";
  CHECK(privacy_sandbox::bidding_auction_servers::inference::
            EnforceModelResetProbability(config)
                .ok())
      << "Could not set the model reset probability.";
  CHECK(privacy_sandbox::bidding_auction_servers::inference::SetTcMallocConfig(
            config)
            .ok())
      << "Could not set TCMalloc config.";

  if (absl::Status run_status =
          privacy_sandbox::bidding_auction_servers::inference::Run(config);
      !run_status.ok()) {
    ABSL_LOG(FATAL) << "Unsuccessful run of the inference sidecar due to "
                    << run_status;
  }
  return 0;
}
