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
  AddMetric(response, "total_requests", 1);
  AddMetric(response, "total_requests", 2);
  AddMetric(response, "total_requests", 3);
  ASSERT_TRUE(response.metrics_list().contains("total_requests"));
  EXPECT_EQ(response.metrics_list().at("total_requests").metrics().size(), 3);
  EXPECT_EQ(
      response.metrics_list().at("total_requests").metrics().at(0).value(), 1);
  EXPECT_EQ(
      response.metrics_list().at("total_requests").metrics().at(1).value(), 2);
  EXPECT_EQ(
      response.metrics_list().at("total_requests").metrics().at(2).value(), 3);
}

TEST(AddMetricTest, AddPartitionedMetric) {
  PredictResponse response;
  AddMetric(response, "error_count", 5, "Not Found");

  ASSERT_TRUE(response.metrics_list().contains("error_count"));
  EXPECT_EQ(response.metrics_list().at("error_count").metrics().at(0).value(),
            5);
  EXPECT_EQ(
      response.metrics_list().at("error_count").metrics().at(0).partition(),
      "Not Found");
}

TEST(AddMetricTest, MultipleMetrics) {
  PredictResponse response;
  AddMetric(response, "total_requests", 150);
  AddMetric(response, "success_count", 140);
  AddMetric(response, "failure_count", 10, "Not Found");
  AddMetric(response, "model_batch", 2, "model 1");
  AddMetric(response, "model_batch", 1, "model 2");

  ASSERT_EQ(response.metrics_list_size(), 4);
  EXPECT_EQ(
      response.metrics_list().at("total_requests").metrics().at(0).value(),
      150);
  EXPECT_EQ(response.metrics_list().at("success_count").metrics().at(0).value(),
            140);
  EXPECT_EQ(response.metrics_list().at("failure_count").metrics().at(0).value(),
            10);
  EXPECT_EQ(
      response.metrics_list().at("failure_count").metrics().at(0).partition(),
      "Not Found");
  EXPECT_EQ(response.metrics_list().at("model_batch").metrics().at(0).value(),
            2);
  EXPECT_EQ(
      response.metrics_list().at("model_batch").metrics().at(0).partition(),
      "model 1");
  EXPECT_EQ(response.metrics_list().at("model_batch").metrics().at(1).value(),
            1);
  EXPECT_EQ(
      response.metrics_list().at("model_batch").metrics().at(1).partition(),
      "model 2");
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
