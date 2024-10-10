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

#ifndef SERVICES_COMMON_CLIENTS_ASYNC_BIDDING_ASYNC_GRPC_CLIENT_STUB_TEST_H_
#define SERVICES_COMMON_CLIENTS_ASYNC_BIDDING_ASYNC_GRPC_CLIENT_STUB_TEST_H_

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
#include "services/common/clients/config/trusted_server_config_client.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/crypto_client_factory.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/encryption/mock_crypto_client_wrapper.h"
#include "services/common/test/mocks.h"
#include "services/common/test/random.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/mock/mock_key_fetcher_manager.h"

namespace privacy_sandbox::bidding_auction_servers {
constexpr char kTestSecret[] = "secret";
constexpr char kTestKeyId[] = "keyid";

using ::testing::AnyNumber;

template <typename Request, typename RawRequest, typename Response,
          typename RawResponse, typename ServiceThread, typename TestClient,
          typename ClientConfig, typename Service>
struct AsyncGrpcClientTypeDefinitions {
  using RequestType = Request;
  using RawRequestType = RawRequest;
  using ResponseType = Response;
  using RawResponseType = RawResponse;
  using ServiceThreadType = ServiceThread;
  using TestClientType = TestClient;
  using ClientConfigType = ClientConfig;
  using ServiceType = Service;
};

template <class AsyncGrpcClientType>
class AsyncGrpcClientStubTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommonTestInit();
    config_client_.SetOverride(kTrue, TEST_MODE);
  }

  TrustedServersConfigClient config_client_{{}};
};

TYPED_TEST_SUITE_P(AsyncGrpcClientStubTest);

template <typename T>
void SetupMockCryptoClientError(T request,
                                MockCryptoClientWrapper& crypto_client) {
  // Mock the HpkeEncrypt() call on the crypto client.
  absl::Status response = absl::InternalError("");
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillOnce(testing::Return(response));
}

template <typename T>
void SetupMockCryptoClientWrapper(T request,
                                  MockCryptoClientWrapper& crypto_client) {
  // Mock the HpkeEncrypt() call on the crypto client.
  google::cmrt::sdk::crypto_service::v1::HpkeEncryptResponse
      hpke_encrypt_response;
  hpke_encrypt_response.set_secret(kTestSecret);
  hpke_encrypt_response.mutable_encrypted_data()->set_key_id(kTestKeyId);
  hpke_encrypt_response.mutable_encrypted_data()->set_ciphertext(
      request.SerializeAsString());
  EXPECT_CALL(crypto_client, HpkeEncrypt)
      .Times(testing::AnyNumber())
      .WillOnce(testing::Return(hpke_encrypt_response));

  // Mock the HpkeDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::HpkeDecryptResponse
      hpke_decrypt_response;
  hpke_decrypt_response.set_payload(request.SerializeAsString());
  hpke_decrypt_response.set_secret(kTestSecret);
  EXPECT_CALL(crypto_client, HpkeDecrypt)
      .Times(AnyNumber())
      .WillRepeatedly(testing::Return(hpke_decrypt_response));

  // Mock the AeadEncrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::AeadEncryptedData data;
  data.set_ciphertext(request.SerializeAsString());
  google::cmrt::sdk::crypto_service::v1::AeadEncryptResponse
      aead_encrypt_response;
  *aead_encrypt_response.mutable_encrypted_data() = std::move(data);
  EXPECT_CALL(crypto_client, AeadEncrypt)
      .Times(AnyNumber())
      .WillOnce(testing::Return(aead_encrypt_response));

  // Mock the AeadDecrypt() call on the crypto_client.
  google::cmrt::sdk::crypto_service::v1::AeadDecryptResponse
      aead_decrypt_response;
  aead_decrypt_response.set_payload(request.SerializeAsString());
  EXPECT_CALL(crypto_client, AeadDecrypt)
      .Times(AnyNumber())
      .WillOnce(testing::Return(aead_decrypt_response));
}

TYPED_TEST_P(AsyncGrpcClientStubTest, CallsServerWithRequest) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

  RawRequest received_request;
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [&received_request](grpc::CallbackServerContext* context,
                          const Request* request, Response* response) {
        received_request.ParseFromString(request->request_ciphertext());
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>(raw_request);
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientWrapper(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, /* public_key_fetcher= */ nullptr);
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  absl::Notification notification;
  grpc::ClientContext context;

  auto status = class_under_test.ExecuteInternal(
      std::move(input_request_ptr), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) { notification.Notify(); });
  CHECK_OK(status);
  notification.WaitForNotification();
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      raw_request, received_request));
}

TYPED_TEST_P(AsyncGrpcClientStubTest, CallsServerWithMetadata) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

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

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>(raw_request);
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientWrapper(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, /* public_key_fetcher= */ nullptr);
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  absl::Notification notification;
  grpc::ClientContext context;
  for (auto& [key, value] : sent_metadata) {
    context.AddMetadata(key, value);
  }

  auto status = class_under_test.ExecuteInternal(
      std::make_unique<RawRequest>(), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) { notification.Notify(); });
  CHECK_OK(status);
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
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [](grpc::CallbackServerContext* context, const Request* request,
         Response* response) {
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                     "Invalid request"));
        return reactor;
      });

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>(raw_request);
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientWrapper(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, CreatePublicKeyFetcher(this->config_client_));
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  absl::Notification notification;
  grpc::ClientContext context;

  auto status = class_under_test.ExecuteInternal(
      std::move(input_request_ptr), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) {
        EXPECT_EQ(get_values_response.status().code(),
                  absl::StatusCode::kInvalidArgument);
        notification.Notify();
      });
  CHECK_OK(status);
  notification.WaitForNotification();
}

TYPED_TEST_P(AsyncGrpcClientStubTest, CallsServerWithTimeout) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

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

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>(raw_request);
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientWrapper(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, CreatePublicKeyFetcher(this->config_client_));
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  grpc::ClientContext context;
  auto status = class_under_test.ExecuteInternal(
      std::move(input_request_ptr), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) { notification.Notify(); },
      timeout);
  CHECK_OK(status);
  notification.WaitForNotification();
  // Time diff in ms is expected due to different invocations of absl::Now(),
  // but should within a small range.
  EXPECT_NEAR(0, time_difference_ms, 200);
}

TYPED_TEST_P(AsyncGrpcClientStubTest, PassesResponseToCallback) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

  RawResponse expected_output;
  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [expected_output](grpc::CallbackServerContext* context,
                        const Request* request, Response* response) {
        response->set_response_ciphertext(expected_output.SerializeAsString());

        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>();
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientWrapper(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, CreatePublicKeyFetcher(this->config_client_));
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  absl::Notification notification;
  std::unique_ptr<Response> output;
  grpc::ClientContext context;

  auto status = class_under_test.ExecuteInternal(
      std::move(input_request_ptr), &context,
      [&notification, &expected_output](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) {
        EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
            **get_values_response, expected_output));
        notification.Notify();
      });
  CHECK_OK(status);
  notification.WaitForNotification();
}

TYPED_TEST_P(AsyncGrpcClientStubTest, DoesNotExecuteCallbackOnSyncError) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [](grpc::CallbackServerContext* context, const Request* request,
         Response* response) {
        auto reactor = context->DefaultReactor();
        absl::SleepFor(absl::Milliseconds(50));
        EXPECT_TRUE(context->IsCancelled());
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>();
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientError(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, CreatePublicKeyFetcher(this->config_client_));
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  absl::Notification notification;
  std::unique_ptr<Response> output;
  grpc::ClientContext context;

  auto status = class_under_test.ExecuteInternal(
      std::move(input_request_ptr), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) {
        EXPECT_FALSE(get_values_response.ok());
        notification.Notify();
      },
      absl::Milliseconds(1));
  EXPECT_FALSE(status.ok());
  notification.WaitForNotificationWithTimeout(absl::Milliseconds(50));
  EXPECT_FALSE(notification.HasBeenNotified());
}

TYPED_TEST_P(AsyncGrpcClientStubTest, ExecutesCallbackOnTimeout) {
  using ServiceThread = typename TypeParam::ServiceThreadType;
  using Request = typename TypeParam::RequestType;
  using RawRequest = typename TypeParam::RawRequestType;
  using Response = typename TypeParam::ResponseType;
  using RawResponse = typename TypeParam::RawResponseType;
  using TestClient = typename TypeParam::TestClientType;
  using ClientConfig = typename TypeParam::ClientConfigType;
  using ServiceType = typename TypeParam::ServiceType;

  auto dummy_service_thread_ = std::make_unique<ServiceThread>(
      [](grpc::CallbackServerContext* context, const Request* request,
         Response* response) {
        auto reactor = context->DefaultReactor();
        absl::SleepFor(absl::Milliseconds(50));
        EXPECT_TRUE(context->IsCancelled());
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  ClientConfig client_config = {
      .server_addr = dummy_service_thread_->GetServerAddr(),
  };
  RawRequest raw_request;
  auto input_request_ptr = std::make_unique<RawRequest>();
  MockCryptoClientWrapper crypto_client;
  SetupMockCryptoClientWrapper(raw_request, crypto_client);
  auto key_fetcher_manager = CreateKeyFetcherManager(
      this->config_client_, CreatePublicKeyFetcher(this->config_client_));
  auto stub = ServiceType::NewStub(CreateChannel(client_config.server_addr,
                                                 client_config.compression,
                                                 client_config.secure_client));
  TestClient class_under_test(key_fetcher_manager.get(), &crypto_client,
                              client_config, stub.get());
  absl::Notification notification;
  std::unique_ptr<Response> output;
  grpc::ClientContext context;

  auto status = class_under_test.ExecuteInternal(
      std::move(input_request_ptr), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) {
        EXPECT_FALSE(get_values_response.ok());
        notification.Notify();
      },
      absl::Milliseconds(10));
  CHECK_OK(status);
  notification.WaitForNotification();
}

REGISTER_TYPED_TEST_SUITE_P(AsyncGrpcClientStubTest, CallsServerWithRequest,
                            PassesStatusToCallback, CallsServerWithTimeout,
                            PassesResponseToCallback, CallsServerWithMetadata,
                            ExecutesCallbackOnTimeout,
                            DoesNotExecuteCallbackOnSyncError);

}  // namespace privacy_sandbox::bidding_auction_servers

#endif  // SERVICES_COMMON_CLIENTS_ASYNC_BIDDING_ASYNC_GRPC_CLIENT_STUB_TEST_H_
