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

#ifndef SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_UTILS_H_
#define SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_executor.h"
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/clients/code_dispatcher/request_context.h"
#include "src/roma/interface/roma.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

constexpr absl::string_view kInferenceFunctionName = "runInference";
constexpr absl::string_view kGetModelPathsFunctionName = "getModelPaths";

// Accesses a sandbox exectuor that uses static storage.
SandboxExecutor& Executor();

// Creates a new stub for inference.
std::unique_ptr<InferenceService::StubInterface> CreateInferenceStub();

// Registers AdTech models with the inference sidecar. These models are
// downloaded to the bidding server and sent to the inference sidecar via IPC.
absl::Status RegisterModelsFromLocal(const std::vector<std::string>& paths);
absl::Status RegisterModelsFromBucket(
    absl::string_view bucket_name, const std::vector<std::string>& paths,
    const std::vector<BlobFetcher::Blob>& blobs);

// Registered with Roma to provide an inference API in JS code. It sends a
// single inference request to the inference sidecar.
//
// wrapper: Inference request backed by JS string.
void RunInference(
    google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext>&
        wrapper);

// Registered with Roma to provide an API to query the currently available
// models from JS code.
void GetModelPaths(
    google::scp::roma::FunctionBindingPayload<RomaRequestSharedContext>&
        wrapper);

// Converts a GetModelPaths response to Json string
std::string GetModelResponseToJson(const GetModelPathsResponse& response);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_BIDDING_SERVICE_INFERENCE_INFERENCE_UTILS_H_
