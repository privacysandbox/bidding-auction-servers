/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "services/common/data_fetch/version_util.h"

#include "absl/status/statusor.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(VersionUtilTest, ReturnsMissingBucketNameIfBucketNameIsEmpty) {
  const absl::StatusOr<std::string> result = GetBucketBlobVersion("", "blob");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), kMissingBucketName);
}

TEST(VersionUtilTest, ReturnsMissingBlobNameIfBlobNameIsEmpty) {
  const absl::StatusOr<std::string> result = GetBucketBlobVersion("bucket", "");
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), kMissingBlobName);
}

TEST(VersionUtilTest, GetsBucketBlobVersion) {
  const absl::StatusOr<std::string> result =
      GetBucketBlobVersion("bucket", "blob");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(*result, "bucket/blob");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
