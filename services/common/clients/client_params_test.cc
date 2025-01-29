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

#include "services/common/clients/client_params.h"

#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "services/common/test/random.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

class MockRequest {
 public:
  MockRequest() { req_int = MakeARandomInt(101, 200); }
  int req_int;
};

class MockResponse {
 public:
  int resp_int;
};

TEST(ClientParamsTest, RequestRefProvidesRequestObject) {
  std::unique_ptr<MockRequest> input_request = std::make_unique<MockRequest>();
  int input_int = input_request->req_int;
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<MockResponse>>) &&>
      callback = [](absl::StatusOr<std::unique_ptr<MockResponse>> response) {};

  ClientParams<MockRequest, MockResponse> class_under_test(
      std::move(input_request), std::move(callback));

  // Checking equality indirectly, since copy elision is not guaranteed
  EXPECT_EQ(input_int, class_under_test.RequestRef()->req_int);
}

TEST(ClientParamsTest, ResponseRefProvidesNonNullResponseObject) {
  std::unique_ptr<MockRequest> input_request = std::make_unique<MockRequest>();
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<MockResponse>>) &&>
      callback = [](absl::StatusOr<std::unique_ptr<MockResponse>> response) {};

  ClientParams<MockRequest, MockResponse> class_under_test(
      std::move(input_request), std::move(callback));

  // Try to access class variable to verify non-null response
  EXPECT_NE(nullptr, class_under_test.ResponseRef());
}

TEST(ClientParamsTest, ContextRefProvidesNonNullContextObject) {
  std::unique_ptr<MockRequest> input_request = std::make_unique<MockRequest>();
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<MockResponse>>) &&>
      callback = [](absl::StatusOr<std::unique_ptr<MockResponse>> response) {};

  ClientParams<MockRequest, MockResponse> class_under_test(
      std::move(input_request), std::move(callback));

  // Try to access class variable to verify non-null context
  EXPECT_NE(nullptr, class_under_test.ContextRef());
}

TEST(ClientParamsTest, SetDeadlineSetsDeadlineValueInContext) {
  std::unique_ptr<MockRequest> input_request = std::make_unique<MockRequest>();
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<MockResponse>>) &&>
      callback = [](absl::StatusOr<std::unique_ptr<MockResponse>> response) {};
  absl::Duration timeout = absl::Microseconds(MakeARandomInt(1, 999));
  auto expected_timeout = absl::Now() + timeout;

  ClientParams<MockRequest, MockResponse> class_under_test(
      std::move(input_request), std::move(callback));
  class_under_test.SetDeadline(timeout);

  // time diff in us is expected due to different invocations of absl::Now(),
  // but should be 0 in ms
  int64_t time_difference_ms = ToInt64Milliseconds(
      absl::FromChrono(class_under_test.ContextRef()->deadline()) -
      expected_timeout);
  EXPECT_EQ(0, time_difference_ms);
}

TEST(ClientParamsTest, OnDoneExecutesCallback) {
  std::unique_ptr<MockRequest> input_request = std::make_unique<MockRequest>();
  int output_value = MakeARandomInt(INT16_MIN, INT16_MAX - 1);
  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<MockResponse>>) &&>
      callback = [&output_value](
                     absl::StatusOr<std::unique_ptr<MockResponse>> response) {
        ASSERT_TRUE(response.ok());
        output_value = response.value()->resp_int;
      };

  auto class_under_test = new ClientParams<MockRequest, MockResponse>(
      std::move(input_request), std::move(callback));
  int expected_value = MakeARandomInt(INT16_MAX, INT32_MAX);
  class_under_test->ResponseRef()->resp_int = expected_value;

  class_under_test->OnDone(grpc::Status::OK);
  EXPECT_EQ(output_value, expected_value);
}

TEST(ClientParamsTest, ReceivesMetadataObject) {
  std::unique_ptr<MockRequest> input_request = std::make_unique<MockRequest>();
  absl::flat_hash_map<std::string, std::string> metadata = MakeARandomMap();

  absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<MockResponse>>) &&>
      callback = [](absl::StatusOr<std::unique_ptr<MockResponse>> response) {};

  // Cannot test the values get set in the client context here because
  // the metadata is not directly accessible. gRPC tests it using a friend
  // class. Tested in
  // services/common/clients/async_grpc/default_async_grpc_client_test.cc
  ClientParams<MockRequest, MockResponse> class_under_test(
      std::move(input_request), std::move(callback), metadata);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
