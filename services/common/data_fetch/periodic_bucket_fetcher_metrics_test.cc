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

#include "services/common/data_fetch/periodic_bucket_fetcher_metrics.h"

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers {
namespace {

TEST(PeriodicBucketFetcherMetricsTest, BlobLoadMetricsReportSuccess) {
  PeriodicBucketFetcherMetrics::ClearStates_TestOnly();
  absl::Status status = absl::OkStatus();
  PeriodicBucketFetcherMetrics::UpdateBlobLoadMetrics("blob1", "bucket1",
                                                      status);

  auto available_blobs = PeriodicBucketFetcherMetrics::GetAvailableBlobs();
  ASSERT_EQ(available_blobs.size(), 1);
  EXPECT_EQ(available_blobs["bucket1/blob1"], 1.0);

  auto blob_load_status = PeriodicBucketFetcherMetrics::GetBlobLoadStatus();
  ASSERT_EQ(blob_load_status.size(), 1);
  EXPECT_EQ(blob_load_status["bucket1/blob1"], 0.0);
}

TEST(PeriodicBucketFetcherMetricsTest, BlobLoadMetricsReportFailure) {
  PeriodicBucketFetcherMetrics::ClearStates_TestOnly();
  absl::Status status = absl::InternalError("Some error");
  PeriodicBucketFetcherMetrics::UpdateBlobLoadMetrics("blob2", "bucket2",
                                                      status);

  EXPECT_TRUE(PeriodicBucketFetcherMetrics::GetAvailableBlobs().empty());
  auto blob_load_status = PeriodicBucketFetcherMetrics::GetBlobLoadStatus();
  ASSERT_EQ(blob_load_status.size(), 1);
  EXPECT_EQ(blob_load_status["bucket2/blob2"], 13.0);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers
