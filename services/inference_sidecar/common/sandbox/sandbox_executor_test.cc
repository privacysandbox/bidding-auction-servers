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

#include "sandbox/sandbox_executor.h"

#include <gmock/gmock-matchers.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server.h>
#include <grpcpp/server_context.h>
#include <grpcpp/server_posix.h>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "proto/testproto.grpc.pb.h"
#include "proto/testproto.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

using ::testing::HasSubstr;
using ::testing::TestWithParam;

constexpr absl::string_view kExitBinary =
    "__main__/sandbox/sandboxee_exit_test_bin";
constexpr absl::string_view kGrpcBinary =
    "__main__/sandbox/sandboxee_grpc_test_bin";
constexpr absl::string_view kIpcBinary =
    "__main__/sandbox/sandboxee_ipc_test_bin";
constexpr absl::string_view kNonExistentBinary = "non_existent_binary_path";

class SandboxExecutorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    absl::SetFlag(&FLAGS_testonly_allow_policies_for_bazel, true);
  }

 private:
  absl::FlagSaver flag_saver_;
};

TEST_F(SandboxExecutorTest, NonExistentBinary) {
  SandboxExecutor executor(GetFilePath(kNonExistentBinary), {""});

  absl::Status status = executor.StartSandboxee();
  ASSERT_EQ(status.code(), absl::StatusCode::kInternal);
  ASSERT_THAT(status.message(), HasSubstr("SETUP_ERROR"));

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::SETUP_ERROR);
}

TEST_F(SandboxExecutorTest, StopBeforeStart) {
  SandboxExecutor executor(GetFilePath(kNonExistentBinary), {""});

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);

  absl::Status status = executor.StartSandboxee();
  ASSERT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(SandboxExecutorTest, DoubleStop) {
  SandboxExecutor executor(GetFilePath(kExitBinary), {""});
  ASSERT_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  // Wait for sandboxee to stop on its own.
  absl::SleepFor(absl::Seconds(1));

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::OK);
  result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::OK);
}

TEST_F(SandboxExecutorTest, Kill) {
  // `sandboxee_ipc_test_bin` waits for the proto message.
  SandboxExecutor executor(GetFilePath(kIpcBinary), {""});
  absl::Status status = executor.StartSandboxee();
  ASSERT_EQ(status.code(), absl::StatusCode::kOk);

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
}

TEST_F(SandboxExecutorTest, DoubleStart) {
  SandboxExecutor executor(GetFilePath(kIpcBinary), {""});
  ASSERT_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);
  ASSERT_EQ(executor.StartSandboxee().code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(SandboxExecutorTest, Ipc) {
  SandboxExecutor executor(GetFilePath(kIpcBinary), {""});
  sandbox2::Comms comms(executor.FileDescriptor());

  ASSERT_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  // Send and receive message.
  Empty request;
  ASSERT_TRUE(comms.SendProtoBuf(request));

  Empty response;
  ASSERT_TRUE(comms.RecvProtoBuf(&response));

  // Wait for sandboxee to stop on its own.
  absl::SleepFor(absl::Seconds(1));

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::OK);
  ASSERT_EQ(result->reason_code(), 0);
}

TEST_F(SandboxExecutorTest, Grpc) {
  SandboxExecutor executor(GetFilePath(kGrpcBinary), {""});
  sandbox2::Comms comms(executor.FileDescriptor());

  ASSERT_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  // Wait for sandboxee to start gRPC server.
  absl::SleepFor(absl::Seconds(1));

  std::shared_ptr<grpc::Channel> client_channel =
      grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                        executor.FileDescriptor());
  std::unique_ptr<TestService::StubInterface> stub =
      TestService::NewStub(client_channel);

  Empty request, response;
  const int kNumIterations = 1000;
  for (int i = 0; i < kNumIterations; i++) {
    grpc::ClientContext context;
    grpc::Status status = stub->Predict(&context, request, &response);
    ASSERT_TRUE(status.ok()) << status.error_message();
  }
  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  ASSERT_EQ(result->reason_code(), 0);
}

TEST_F(SandboxExecutorTest, ConcurrentGrpc) {
  SandboxExecutor executor(GetFilePath(kGrpcBinary), {""});
  sandbox2::Comms comms(executor.FileDescriptor());

  ASSERT_EQ(executor.StartSandboxee().code(), absl::StatusCode::kOk);

  // Wait for sandboxee to start gRPC server.
  absl::SleepFor(absl::Seconds(1));

  std::shared_ptr<grpc::Channel> client_channel =
      grpc::CreateInsecureChannelFromFd("GrpcChannel",
                                        executor.FileDescriptor());
  std::unique_ptr<TestService::StubInterface> stub =
      TestService::NewStub(client_channel);

  const int kNumThreads = 1000;
  const int kNumIterations = 5;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    threads.push_back(std::thread([&stub, &kNumIterations]() {
      Empty request, response;
      for (int j = 0; j < kNumIterations; j++) {
        grpc::ClientContext context;
        grpc::Status status = stub->Predict(&context, request, &response);
        EXPECT_TRUE(status.ok()) << status.error_message();
      }
    }));
  }

  // Waits for all threads to finish.
  for (auto& thread : threads) {
    thread.join();
  }

  absl::StatusOr<sandbox2::Result> result = executor.StopSandboxee();
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result->final_status(), sandbox2::Result::EXTERNAL_KILL);
  ASSERT_EQ(result->reason_code(), 0);
}

TEST(SandboxeeStateToString, NotStarted) {
  EXPECT_EQ(SandboxeeStateToString(SandboxeeState::kNotStarted), "NotStarted");
}

TEST(SandboxeeStateToString, Running) {
  EXPECT_EQ(SandboxeeStateToString(SandboxeeState::kRunning), "Running");
}

TEST(SandboxeeStateToString, Stopped) {
  EXPECT_EQ(SandboxeeStateToString(SandboxeeState::kStopped), "Stopped");
}

TEST(SandboxeeStateToString, Invalid) {
  ASSERT_EQ(SandboxeeStateToString(static_cast<SandboxeeState>(-1)), "-1");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
