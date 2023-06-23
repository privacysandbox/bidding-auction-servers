// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/common/util/status_util.h"

#include <utility>
#include <vector>

#include <gmock/gmock-matchers.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

constexpr std::pair<grpc::StatusCode, absl::StatusCode> status_mappings[] = {
    {grpc::StatusCode::OK, absl::StatusCode::kOk},                // 0
    {grpc::StatusCode::CANCELLED, absl::StatusCode::kCancelled},  // 1
    {grpc::StatusCode::UNKNOWN, absl::StatusCode::kUnknown},      // 2
    {grpc::StatusCode::INVALID_ARGUMENT,
     absl::StatusCode::kInvalidArgument},  // 3
    {grpc::StatusCode::DEADLINE_EXCEEDED,
     absl::StatusCode::kDeadlineExceeded},                                 // 4
    {grpc::StatusCode::NOT_FOUND, absl::StatusCode::kNotFound},            // 5
    {grpc::StatusCode::ALREADY_EXISTS, absl::StatusCode::kAlreadyExists},  // 6
    {grpc::StatusCode::PERMISSION_DENIED,
     absl::StatusCode::kPermissionDenied},  // 7
    {grpc::StatusCode::RESOURCE_EXHAUSTED,
     absl::StatusCode::kResourceExhausted},  // 8
    {grpc::StatusCode::FAILED_PRECONDITION,
     absl::StatusCode::kFailedPrecondition},                              // 9
    {grpc::StatusCode::ABORTED, absl::StatusCode::kAborted},              // 10
    {grpc::StatusCode::OUT_OF_RANGE, absl::StatusCode::kOutOfRange},      // 11
    {grpc::StatusCode::UNIMPLEMENTED, absl::StatusCode::kUnimplemented},  // 12
    {grpc::StatusCode::INTERNAL, absl::StatusCode::kInternal},            // 13
    {grpc::StatusCode::UNAVAILABLE, absl::StatusCode::kUnavailable},      // 14
    {grpc::StatusCode::DATA_LOSS, absl::StatusCode::kDataLoss},           // 15
    {grpc::StatusCode::UNAUTHENTICATED,
     absl::StatusCode::kUnauthenticated},  // 16
};

TEST(StatusUtilTest, GrpcToAbsl) {
  for (const auto& mapping : status_mappings) {
    auto status = grpc::Status(mapping.first, "error message");
    auto expected_message = status.error_code() == grpc::StatusCode::OK
                                ? ""
                                : status.error_message();
    auto converted = ToAbslStatus(status);
    EXPECT_THAT(converted.code(), mapping.second);
    EXPECT_THAT(converted.message(), expected_message);
  }
}

TEST(StatusUtilTest, AbslToGrpc) {
  for (const auto& mapping : status_mappings) {
    auto status = absl::Status(mapping.second, "error message");
    auto expected_message =
        status.code() == absl::StatusCode::kOk ? "" : status.message();
    auto converted = FromAbslStatus(status);
    EXPECT_THAT(converted.error_code(), mapping.first);
    EXPECT_THAT(converted.error_message(), expected_message);
  }
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
