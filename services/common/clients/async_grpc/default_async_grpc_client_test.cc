//  Copyright 2022 Google LLC
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

#include "services/common/clients/async_grpc/default_async_grpc_client.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

// The structs below have some definitions to mimic protos.
// default_async_grpc_client expects these fields to exist on the template types
// provided during instantiation.
struct RawRequest {
  std::string SerializeAsString() { return ""; }
};

struct MockRequest {
  int value;
  RawRequest raw_request_field;

  void set_request_ciphertext(absl::string_view cipher) {}
  void clear_raw_request() {}
  RawRequest raw_request() { return raw_request_field; }
  void set_key_id(absl::string_view key_id) {}
};

struct RawResponse {
  void ParseFromString(absl::string_view) {}
};

class MockResponse {
 public:
  int value;
  RawResponse raw_response;
  std::string ciphertext;

  RawResponse* mutable_raw_response() { return &raw_response; }
  void clear_response_ciphertext() {}
  std::string& response_ciphertext() { return ciphertext; }
};

class TestDefaultAsyncGrpcClient
    : public DefaultAsyncGrpcClient<MockRequest, MockResponse, RawResponse> {
 public:
  TestDefaultAsyncGrpcClient(
      server_common::KeyFetcherManagerInterface* key_fetcher_manager,
      CryptoClientWrapperInterface* crypto_client, bool encryption_enabled,
      absl::Notification& notification, MockRequest req,
      absl::Duration expected_timeout)
      : DefaultAsyncGrpcClient(key_fetcher_manager, crypto_client,
                               encryption_enabled),
        notification_(notification),
        req_(req),
        expected_timeout_(expected_timeout) {}
  absl::Status SendRpc(ClientParams<MockRequest, MockResponse>* params,
                       absl::string_view hpke_secret) const override {
    EXPECT_EQ(params->RequestRef()->value, req_.value);
    absl::Duration actual_timeout =
        absl::FromChrono(params->ContextRef()->deadline()) - absl::Now();

    int64_t time_difference_ms =
        ToInt64Milliseconds(actual_timeout - expected_timeout_);
    // Time diff in ms is expected due to different invocations of absl::Now(),
    // but should within a small range.
    EXPECT_GE(time_difference_ms, 0);
    EXPECT_LE(time_difference_ms, 100);
    notification_.Notify();
    params->OnDone(grpc::Status::OK);
    return absl::OkStatus();
  }
  absl::Notification& notification_;
  MockRequest req_;
  absl::Duration expected_timeout_;
};

TEST(TestDefaultAsyncGrpcClient, SendsMessageWithCorrectParams) {
  absl::Notification notification;
  MockRequest req;
  RequestMetadata metadata = MakeARandomMap();
  req.value = 1;

  absl::Duration timeout_ms = absl::Milliseconds(100);
  TestDefaultAsyncGrpcClient client(nullptr, nullptr, false, notification, req,
                                    timeout_ms);

  client.Execute(
      std::make_unique<MockRequest>(req), metadata,
      [](absl::StatusOr<std::unique_ptr<MockResponse>> result) {}, timeout_ms);
  notification.WaitForNotification();
}
}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
