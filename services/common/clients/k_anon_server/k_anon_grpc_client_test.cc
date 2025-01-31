//  Copyright 2024 Google LLC
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

#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "services/common/clients/k_anon_server/k_anon_client.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using Request = ValidateHashesRequest;
using Response = ValidateHashesResponse;
using ServiceThread = MockServerThread<KAnonServiceMock, Request, Response>;
using ClientConfig = KAnonClientConfig;
using TestClient = KAnonGrpcClient;

class KAnonAsyncGrpcTest : public ::testing::Test {};

TEST_F(KAnonAsyncGrpcTest, CallsServerWithRequest) {
  Request received_request;
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&received_request](grpc::CallbackServerContext* context,
                          const Request* request, Response* response) {
        received_request = *request;
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config;
  client_config.server_addr = dummy_service_thread_->GetServerAddr();
  client_config.ca_root_pem = kTestCaCertPath;

  TestClient class_under_test(client_config);
  absl::Notification notification;
  Request request;
  auto input_request_ptr = std::make_unique<Request>(request);

  absl::Status status = class_under_test.Execute(
      std::move(input_request_ptr),
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        notification.Notify();
      });
  notification.WaitForNotification();
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      request, received_request));
}

TEST_F(KAnonAsyncGrpcTest, PassesStatusToCallback) {
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [](grpc::CallbackServerContext* context, const Request* request,
         Response* response) {
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                     "Invalid request"));
        return reactor;
      });

  ClientConfig client_config;
  client_config.server_addr = dummy_service_thread_->GetServerAddr();
  client_config.ca_root_pem = kTestCaCertPath;

  TestClient class_under_test(client_config);
  auto input_request_ptr = std::make_unique<Request>();
  absl::Notification notification;

  auto status = class_under_test.Execute(
      std::move(input_request_ptr),
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        EXPECT_EQ(get_values_response.status().code(),
                  absl::StatusCode::kInvalidArgument);
        notification.Notify();
      });
  CHECK_OK(status);
  notification.WaitForNotification();
}

TEST_F(KAnonAsyncGrpcTest, CallsServerWithTimeout) {
  auto input_request_ptr = std::make_unique<Request>();
  absl::Notification notification;
  absl::Duration timeout = absl::Milliseconds(MakeARandomInt(50, 100));
  auto expected_timeout = absl::Now() + timeout;
  int64_t time_difference_ms = 0;

  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&expected_timeout, &time_difference_ms](
          grpc::CallbackServerContext* context, const Request* request,
          Response* response) {
        auto reactor = context->DefaultReactor();
        absl::Time actual_timeout = absl::FromChrono(context->deadline());
        if (actual_timeout > expected_timeout) {
          time_difference_ms =
              ToInt64Milliseconds(actual_timeout - expected_timeout);
        } else {
          time_difference_ms =
              ToInt64Milliseconds(expected_timeout - actual_timeout);
        }
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config;
  client_config.server_addr = dummy_service_thread_->GetServerAddr();
  client_config.ca_root_pem = kTestCaCertPath;

  TestClient class_under_test(client_config);
  auto status = class_under_test.Execute(
      std::move(input_request_ptr),
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        notification.Notify();
      },
      timeout);
  CHECK_OK(status);
  notification.WaitForNotification();
  // Time diff in ms is expected due to different invocations of absl::Now(),
  // but should within a small range.
  EXPECT_NEAR(0, time_difference_ms, 200);
}

TEST_F(KAnonAsyncGrpcTest, PassesResponseToCallback) {
  Response expected_output;
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [expected_output](grpc::CallbackServerContext* context,
                        const Request* request, Response* response) {
        *response = expected_output;

        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config;
  client_config.server_addr = dummy_service_thread_->GetServerAddr();
  client_config.ca_root_pem = kTestCaCertPath;

  TestClient class_under_test(client_config);
  absl::Notification notification;
  std::unique_ptr<Response> output;
  auto input_request_ptr = std::make_unique<Request>();

  auto status = class_under_test.Execute(
      std::move(input_request_ptr),
      [&notification, &expected_output](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            *get_values_response.value().get(), expected_output));
        notification.Notify();
      });
  CHECK_OK(status);
  notification.WaitForNotification();
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
