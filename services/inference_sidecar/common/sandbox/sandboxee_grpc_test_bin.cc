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

// Sandboxee binary to test gRPC over IPC.

#include <fcntl.h>

#include <memory>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "proto/testproto.grpc.pb.h"
#include "proto/testproto.pb.h"
#include "sandbox/sandbox_worker.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

// Test service implementation.
class TestServiceImpl final : public TestService::Service {
 public:
  TestServiceImpl() = default;

  grpc::Status Predict(grpc::ServerContext* context, const Empty* request,
                       Empty* response) override {
    ABSL_LOG(INFO) << "Received Predict gRPC call";
    return grpc::Status::OK;
  }
};

void Run() {
  SandboxWorker worker;
  grpc::ServerBuilder builder;

  auto server_impl = std::make_unique<TestServiceImpl>();
  builder.RegisterService(server_impl.get());
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    ABSL_LOG(ERROR) << "Cannot start the server.";
    return;
  }

  // Set up gRPC over IPC.
  AddInsecureChannelFromFd(server.get(), worker.FileDescriptor());
  server->Wait();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference

int main(int argc, char** argv) {
  privacy_sandbox::bidding_auction_servers::inference::Run();
  return 0;
}
