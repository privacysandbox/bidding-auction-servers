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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_RAW_ASYNC_GRPC_CLIENT_STUB_TEST_H_
#define SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_RAW_ASYNC_GRPC_CLIENT_STUB_TEST_H_

#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include <grpcpp/grpcpp.h>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "api/bidding_auction_servers.pb.h"
#include "google/protobuf/util/message_differencer.h"
#include "services/common/clients/client_params.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {

template <typename Request, typename RawRequest, typename Response,
          typename RawResponse, typename ServiceThread, typename TestClient,
          typename ClientConfig>
struct AsyncGrpcClientTypeDefinitions {
  using RequestType = Request;
  using RawRequestType = RawRequest;
  using ResponseType = Response;
  using RawResponseType = RawResponse;
  using ServiceThreadType = ServiceThread;
  using TestClientType = TestClient;
  using ClientConfigType = ClientConfig;
};

template <class AsyncGrpcClientType>
class AsyncGrpcClientStubTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(AsyncGrpcClientStubTest);

TYPED_TEST_P(AsyncGrpcClientStubTest, CallsServerWithRequest) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using Response = typename TypeParam::ResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;

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

  TestClient class_under_test(nullptr, nullptr, client_config);
  absl::Notification notification;
  Request request;
  auto input_request_ptr = std::make_unique<Request>(request);

  class_under_test.Execute(
      std::move(input_request_ptr), {},
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        notification.Notify();
      });
  notification.WaitForNotification();
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      request, received_request));
}

TYPED_TEST_P(AsyncGrpcClientStubTest, CallsServerWithMetadata) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using Response = typename TypeParam::ResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;

  RequestMetadata sent_metadata = MakeARandomMap();
  RequestMetadata received_metadata;

  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&received_metadata](grpc::CallbackServerContext* context,
                           const Request* request, Response* response) {
        auto reactor = context->DefaultReactor();
        // copy metadata into received object.
        for (const auto& it : context->client_metadata()) {
          // grpc::string_ref is not NULL terminated.
          // https://github.com/grpc/grpc/issues/21288
          received_metadata.try_emplace(
              std::string(it.first.begin(), it.first.end()),
              std::string(it.second.begin(), it.second.end()));
        }
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config;
  client_config.server_addr = dummy_service_thread_->GetServerAddr();

  TestClient class_under_test(nullptr, nullptr, client_config);
  absl::Notification notification;
  class_under_test.Execute(
      std::make_unique<Request>(), sent_metadata,
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        notification.Notify();
      });
  notification.WaitForNotification();

  // GRPC might have added some more headers.
  ASSERT_TRUE(received_metadata.size() >= sent_metadata.size());
  // Verify all metadata was received by server.
  for (const auto& [key, value] : sent_metadata) {
    ASSERT_TRUE(received_metadata.find(key) != received_metadata.end());
    EXPECT_STREQ(received_metadata.find(key)->second.data(), value.data());
  }
}

TYPED_TEST_P(AsyncGrpcClientStubTest, PassesStatusToCallback) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using Response = typename TypeParam::ResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;

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

  TestClient class_under_test(nullptr, nullptr, client_config);
  auto input_request_ptr = std::make_unique<Request>();
  absl::Notification notification;

  class_under_test.Execute(
      std::move(input_request_ptr), {},
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        EXPECT_EQ(get_values_response.status().code(),
                  absl::StatusCode::kInvalidArgument);
        notification.Notify();
      });

  notification.WaitForNotification();
}

TYPED_TEST_P(AsyncGrpcClientStubTest, CallsServerWithTimeout) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using Response = typename TypeParam::ResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;

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

  TestClient class_under_test(nullptr, nullptr, client_config);
  class_under_test.Execute(
      std::move(input_request_ptr), {},
      [&notification](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        notification.Notify();
      },
      timeout);
  notification.WaitForNotification();
  // Time diff in ms is expected due to different invocations of absl::Now(),
  // but should within a small range.
  EXPECT_NEAR(0, time_difference_ms, 200);
}

TYPED_TEST_P(AsyncGrpcClientStubTest, PassesResponseToCallback) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using Response = typename TypeParam::ResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;

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

  TestClient class_under_test(nullptr, nullptr, client_config);
  absl::Notification notification;
  std::unique_ptr<Response> output;
  auto input_request_ptr = std::make_unique<Request>();

  class_under_test.Execute(
      std::move(input_request_ptr), {},
      [&notification, &expected_output](
          absl::StatusOr<std::unique_ptr<Response>> get_values_response) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            *get_values_response.value().get(), expected_output));
        notification.Notify();
      });

  notification.WaitForNotification();
}

REGISTER_TYPED_TEST_SUITE_P(AsyncGrpcClientStubTest, CallsServerWithRequest,
                            PassesStatusToCallback, CallsServerWithTimeout,
                            PassesResponseToCallback, CallsServerWithMetadata);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_GRPC_DEFAULT_RAW_ASYNC_GRPC_CLIENT_STUB_TEST_H_
