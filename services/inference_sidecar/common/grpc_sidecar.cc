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

// Runs a simple gRPC server.

#include "grpc_sidecar.h"

#include <memory>
#include <string>
#include <utility>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_worker.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Inference service implementation.
class InferenceServiceImpl final : public InferenceService::Service {
 public:
  InferenceServiceImpl(std::unique_ptr<ModuleInterface> inference_module)
      : inference_module_(std::move(inference_module)) {}

  grpc::Status RegisterModel(grpc::ServerContext* context,
                             const RegisterModelRequest* request,
                             RegisterModelResponse* response) override {
    absl::StatusOr<RegisterModelResponse> register_model_response =
        inference_module_->RegisterModel(*request);
    if (!register_model_response.ok()) {
      return server_common::FromAbslStatus(register_model_response.status());
    }
    *response = register_model_response.value();
    return grpc::Status::OK;
  }

  grpc::Status Predict(grpc::ServerContext* context,
                       const PredictRequest* request,
                       PredictResponse* response) override {
    absl::StatusOr<PredictResponse> predict_response =
        inference_module_->Predict(*request);
    if (!predict_response.ok()) {
      return server_common::FromAbslStatus(predict_response.status());
    }
    *response = predict_response.value();
    return grpc::Status::OK;
  }

 private:
  std::unique_ptr<ModuleInterface> inference_module_;
};

}  // namespace

absl::Status Run() {
  SandboxWorker worker;
  grpc::ServerBuilder builder;

  auto server_impl =
      std::make_unique<InferenceServiceImpl>(ModuleInterface::Create());
  builder.RegisterService(server_impl.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    ABSL_LOG(ERROR) << "Cannot start the gRPC sidecar.";
    return absl::UnavailableError("Cannot start the gRPC sidecar");
  }

  // Starts up gRPC over IPC.
  grpc::AddInsecureChannelFromFd(server.get(), worker.FileDescriptor());
  server->Wait();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
