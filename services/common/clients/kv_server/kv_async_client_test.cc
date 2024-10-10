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

#include "services/common/clients/kv_server/kv_async_client.h"

#include "googletest/include/gtest/gtest.h"
#include "gtest/gtest.h"
#include "public/query/v2/get_values_v2.grpc.pb.h"
#include "public/query/v2/get_values_v2.pb.h"
#include "services/common/constants/common_service_flags.h"
#include "services/common/encryption/key_fetcher_factory.h"
#include "services/common/test/mocks.h"
#include "services/common/test/utils/test_init.h"
#include "src/encryption/key_fetcher/interface/key_fetcher_manager_interface.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

using Request = kv_server::v2::ObliviousGetValuesRequest;
using RawRequest = kv_server::v2::GetValuesRequest;
using RawResponse = kv_server::v2::GetValuesResponse;
using Response = google::api::HttpBody;
using ServiceThread = MockServerThread<KVServiceMock, Request, Response>;
using TestClient = KVAsyncGrpcClient;
using server_common::KeyFetcherManagerInterface;
using ::testing::AnyNumber;

std::unique_ptr<kv_server::v2::KeyValueService::Stub> CreateStub(
    absl::string_view server_addr) {
  return kv_server::v2::KeyValueService::NewStub(
      CreateChannel(server_addr, /*compression=*/true, /*secure=*/true));
}

class KVAsyncGrpcTest : public ::testing::Test {
 public:
  void SetUp() override {
    CommonTestInit();
    config_client_.SetOverride(kTrue, TEST_MODE);
    key_fetcher_manager_ = CreateKeyFetcherManager(
        config_client_, /* public_key_fetcher=*/nullptr);
  }

 protected:
  TrustedServersConfigClient config_client_{{}};
  std::unique_ptr<KeyFetcherManagerInterface> key_fetcher_manager_;
};

TEST_F(KVAsyncGrpcTest, CallsServerWithRequest) {
  Request received_request;
  auto fake_service_thread = std::make_unique<ServiceThread>(
      [&received_request](grpc::CallbackServerContext* context,
                          const Request* request, Response* response) {
        received_request = *request;
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
      });

  TestClient class_under_test(key_fetcher_manager_.get(),
                              CreateStub(fake_service_thread->GetServerAddr()));
  absl::Notification notification;
  grpc::ClientContext context;

  RawRequest raw_request;
  absl::Status status = class_under_test.ExecuteInternal(
      std::make_unique<RawRequest>(raw_request), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) { notification.Notify(); });
  notification.WaitForNotification();
  EXPECT_TRUE(status.ok()) << status;
}

TEST_F(KVAsyncGrpcTest, CallsServerWithMetadata) {
  RequestMetadata sent_metadata = {{"key", "val"}};
  RequestMetadata received_metadata;

  Request received_request;
  auto fake_service_thread = std::make_unique<ServiceThread>(
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

  TestClient class_under_test(key_fetcher_manager_.get(),
                              CreateStub(fake_service_thread->GetServerAddr()));
  absl::Notification notification;
  grpc::ClientContext context;
  for (auto& [key, value] : sent_metadata) {
    context.AddMetadata(key, value);
  }

  RawRequest raw_request;
  absl::Status status = class_under_test.ExecuteInternal(
      std::make_unique<RawRequest>(raw_request), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) { notification.Notify(); });
  notification.WaitForNotification();
  ASSERT_TRUE(status.ok()) << status;

  // GRPC might have added some more headers.
  ASSERT_TRUE(received_metadata.size() >= sent_metadata.size());
  // Verify all metadata was received by server.
  for (const auto& [key, value] : sent_metadata) {
    ASSERT_TRUE(received_metadata.find(key) != received_metadata.end());
    EXPECT_EQ(received_metadata.find(key)->second, value);
  }
}

TEST_F(KVAsyncGrpcTest, PassesStatusToCallback) {
  auto fake_service_thread = std::make_unique<ServiceThread>(
      [](grpc::CallbackServerContext* context, const Request* request,
         Response* response) {
        auto reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                     "Invalid request"));
        return reactor;
      });

  TestClient class_under_test(key_fetcher_manager_.get(),
                              CreateStub(fake_service_thread->GetServerAddr()));
  absl::Notification notification;
  grpc::ClientContext context;

  RawRequest raw_request;
  absl::Status status = class_under_test.ExecuteInternal(
      std::make_unique<RawRequest>(raw_request), &context,
      [&notification](
          absl::StatusOr<std::unique_ptr<RawResponse>> get_values_response,
          ResponseMetadata response_metadata) {
        EXPECT_EQ(get_values_response.status().code(),
                  absl::StatusCode::kInvalidArgument);
        notification.Notify();
      });
  notification.WaitForNotification();
  EXPECT_TRUE(status.ok()) << status;
}

// TODO(b/350747239): Add a test for response unpadding

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
