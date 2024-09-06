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
#include "utils/inference_metric_util.h"

#include "googletest/include/gtest/gtest.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

TEST(AddMetricTest, AddSimpleMetric) {
  PredictResponse response;
  AddMetric(response, "total_requests", 100);

  ASSERT_TRUE(response.metrics().contains("total_requests"));
  EXPECT_EQ(response.metrics().at("total_requests").value(), 100);
}

TEST(AddMetricTest, AddPartitionedMetric) {
  PredictResponse response;
  AddMetric(response, "error_count", 5, "Not Found");

  ASSERT_TRUE(response.metrics().contains("error_count"));
  EXPECT_EQ(response.metrics().at("error_count").value(), 5);
  EXPECT_EQ(response.metrics().at("error_count").partition(), "Not Found");
}

TEST(AddMetricTest, MultipleMetrics) {
  PredictResponse response;
  AddMetric(response, "total_requests", 150);
  AddMetric(response, "success_count", 140);
  AddMetric(response, "failure_count", 10, "Not Found");

  ASSERT_EQ(response.metrics_size(), 3);
  EXPECT_EQ(response.metrics().at("total_requests").value(), 150);
  EXPECT_EQ(response.metrics().at("success_count").value(), 140);
  EXPECT_EQ(response.metrics().at("failure_count").value(), 10);
  EXPECT_EQ(response.metrics().at("failure_count").partition(), "Not Found");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
