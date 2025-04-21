//  Copyright 2025 Google LLC
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

#include "services/common/util/status_utils.h"

#include <gmock/gmock.h>

#include <string>

#include "absl/types/span.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(FirstNotOkStatusTest, EmptyInput) {
  absl::Status result = FirstNotOkStatus(absl::Span<absl::Status>());
  EXPECT_TRUE(result.ok());
}

TEST(FirstNotOkStatusTest, AllOk) {
  absl::Status statuses[] = {absl::OkStatus(), absl::OkStatus(),
                             absl::OkStatus()};
  absl::Status result = FirstNotOkStatus(absl::MakeSpan(statuses));
  EXPECT_EQ(result, absl::OkStatus());
}

TEST(FirstNotOkStatusTest, FirstNotOk) {
  absl::Status statuses[] = {absl::InvalidArgumentError("First"),
                             absl::OkStatus(), absl::OkStatus()};
  absl::Status result = FirstNotOkStatus(absl::MakeSpan(statuses));
  EXPECT_EQ(result, absl::InvalidArgumentError("First"));
}

TEST(FirstNotOkStatusTest, MiddleNotOk) {
  absl::Status statuses[] = {absl::OkStatus(),
                             absl::DeadlineExceededError("Middle"),
                             absl::OkStatus()};
  absl::Status result = FirstNotOkStatus(absl::MakeSpan(statuses));
  EXPECT_EQ(result, absl::DeadlineExceededError("Middle"));
}

TEST(FirstNotOkStatusTest, LastNotOk) {
  absl::Status statuses[] = {absl::OkStatus(), absl::OkStatus(),
                             absl::PermissionDeniedError("Last")};
  absl::Status result = FirstNotOkStatus(absl::MakeSpan(statuses));
  EXPECT_EQ(result, absl::PermissionDeniedError("Last"));
}

TEST(FirstNotOkStatusTest, MultipleNotOk) {
  absl::Status statuses[] = {absl::InternalError("First Not Ok"),
                             absl::InvalidArgumentError("Second Not Ok")};
  absl::Status result = FirstNotOkStatus(absl::MakeSpan(statuses));
  EXPECT_EQ(result, absl::InternalError("First Not Ok"));
}

TEST(FirstNotOkStatusOrTest, EmptyArguments) {
  absl::Status result = FirstNotOkStatusOr();
  EXPECT_EQ(result, absl::OkStatus());
}

TEST(FirstNotOkStatusOrTest, AllOk) {
  absl::StatusOr<int> ok1 = 1;
  absl::StatusOr<std::string> ok2 = "middle";
  absl::StatusOr<std::string> ok3 = "last";
  absl::Status result = FirstNotOkStatusOr(ok1, ok2, ok3);
  EXPECT_EQ(result, absl::OkStatus());
}

TEST(FirstNotOkStatusOrTest, FirstNotOk) {
  absl::StatusOr<int> not_ok = absl::InvalidArgumentError("First Error");
  absl::StatusOr<std::string> ok = "second";
  absl::Status result = FirstNotOkStatusOr(not_ok, ok);
  EXPECT_EQ(result, absl::InvalidArgumentError("First Error"));
}

TEST(FirstNotOkStatusOrTest, MiddleNotOk) {
  absl::StatusOr<int> ok1 = 1;
  absl::StatusOr<std::string> not_ok =
      absl::DeadlineExceededError("Middle Error");
  absl::StatusOr<int> ok2 = 2;
  absl::Status result = FirstNotOkStatusOr(ok1, not_ok, ok2);
  EXPECT_EQ(result, absl::DeadlineExceededError("Middle Error"));
}

TEST(FirstNotOkStatusOrTest, LastNotOk) {
  absl::StatusOr<int> ok1 = 1;
  absl::StatusOr<std::string> ok2 = "middle";
  absl::StatusOr<bool> not_ok = absl::PermissionDeniedError("Last Error");
  absl::Status result = FirstNotOkStatusOr(ok1, ok2, not_ok);
  EXPECT_EQ(result, absl::PermissionDeniedError("Last Error"));
}

TEST(FirstNotOkStatusOrTest, MultipleNotOk) {
  absl::StatusOr<int> not_ok1 = absl::InternalError("First Not Ok");
  absl::StatusOr<std::string> not_ok2 = absl::UnavailableError("Second Not Ok");
  absl::Status result = FirstNotOkStatusOr(not_ok1, not_ok2);
  EXPECT_EQ(result, absl::InternalError("First Not Ok"));
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
