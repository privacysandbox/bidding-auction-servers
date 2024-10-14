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
#include <vector>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/container/flat_hash_set.h"
#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "sandbox/sandbox_worker.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "src/util/status_macro/status_util.h"
#include "utils/cpu.h"
#include "utils/log.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Uses 600000 ms for 10 mins.
constexpr int kGrpcServerHandshakeTimeoutMs = 600000;
constexpr int kGrpcKeepAliveTimeoutMs = 600000;

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

    // save the model path since it was registered successfully
    absl::WriterMutexLock write_model_paths_lock(&model_paths_mutex_);
    model_paths_.insert(request->model_spec().model_path());

    *response = register_model_response.value();

    return grpc::Status::OK;
  }

  grpc::Status Predict(grpc::ServerContext* context,
                       const PredictRequest* request,
                       PredictResponse* response) override {
    RequestContext request_context(
        [response] { return response->mutable_debug_info(); },
        request->is_consented());
    absl::StatusOr<PredictResponse> predict_response =
        inference_module_->Predict(*request, request_context);
    if (!predict_response.ok()) {
      ABSL_LOG(ERROR) << predict_response.status().message();
      return server_common::FromAbslStatus(predict_response.status());
    }
    *(response->mutable_metrics_list()) =
        std::move(*predict_response->mutable_metrics_list());
    response->set_output(std::move(*predict_response->mutable_output()));

    return grpc::Status::OK;
  }

  // TODO: b/348968123) - Relook at API implementation
  grpc::Status GetModelPaths(grpc::ServerContext* context,
                             const GetModelPathsRequest* request,
                             GetModelPathsResponse* response) override {
    absl::ReaderMutexLock read_model_paths_lock(&model_paths_mutex_);

    for (std::string model_path : model_paths_) {
      ModelSpec* model_spec = response->add_model_specs();
      model_spec->set_model_path(model_path);
    }

    return grpc::Status::OK;
  }

 private:
  std::unique_ptr<ModuleInterface> inference_module_;
  mutable absl::Mutex model_paths_mutex_;
  absl::flat_hash_set<std::string> model_paths_
      ABSL_GUARDED_BY(model_paths_mutex_);
};

}  // namespace

absl::Status SetCpuAffinity(const InferenceSidecarRuntimeConfig& config) {
  if (config.cpuset().empty()) {
    return absl::OkStatus();
  }
  std::vector<int> cpuset(config.cpuset().begin(), config.cpuset().end());
  return SetCpuAffinity(cpuset);
}

absl::Status EnforceModelResetProbability(
    InferenceSidecarRuntimeConfig& config) {
  if (config.model_reset_probability() != 0.0) {
    return absl::InvalidArgumentError(
        "model_reset_probability should not be set");
  }
  config.set_model_reset_probability(kMinResetProbability);
  return absl::OkStatus();
}

absl::Status Run(const InferenceSidecarRuntimeConfig& config) {
  SandboxWorker worker;
  grpc::ServerBuilder builder;
  // Sets timeouts to prevent connection closure during slow model loading.
  // Along with the keepalive timeout, a handshake timeout is necessary
  // to handle cases where cloud model fetching on the bidding server delays the
  // handshake process, ensuring the server doesn't time out while waiting for
  // the handshake.
  builder.AddChannelArgument(GRPC_ARG_SERVER_HANDSHAKE_TIMEOUT_MS,
                             kGrpcServerHandshakeTimeoutMs);
  builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS,
                             kGrpcKeepAliveTimeoutMs);
  // Sets the max receive size to unlimited; The default max size is only 4MB.
  builder.SetMaxReceiveMessageSize(-1);

  if (!config.module_name().empty() &&
      config.module_name() != ModuleInterface::GetModuleVersion()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Expected inference module: ", config.module_name(),
                     ", but got : ", ModuleInterface::GetModuleVersion()));
  }
  auto server_impl =
      std::make_unique<InferenceServiceImpl>(ModuleInterface::Create(config));
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
