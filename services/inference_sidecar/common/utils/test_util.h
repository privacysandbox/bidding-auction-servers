//  Copyright 2024 Google LLC
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

#ifndef PRIVACY_SANDBOX_BIDDING_AUCTION_SERVERS_TEST_UTIL_H_
#define PRIVACY_SANDBOX_BIDDING_AUCTION_SERVERS_TEST_UTIL_H_

#include <string>

#include "gtest/gtest.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Checks a metric in the given map for the specified conditions.
inline void CheckMetric(
    const google::protobuf::Map<std::string, MetricValue>& metrics,
    const std::string& key, int expected_value,
    const std::string& expected_partition = "") {
  auto it = metrics.find(key);
  ASSERT_NE(it, metrics.end()) << "Metric " << key << " is missing.";
  EXPECT_EQ(it->second.value(), expected_value)
      << "Unexpected value for metric " << key;
  if (!expected_partition.empty()) {
    EXPECT_EQ(it->second.partition(), expected_partition)
        << "Unexpected partition for metric " << key;
  }
}

inline void CheckMetricList(
    const google::protobuf::Map<std::string, MetricValueList>& metrics_list,
    const std::string& key, int index, int expected_value,
    const std::string& expected_partition = "") {
  auto it = metrics_list.find(key);
  ASSERT_NE(it, metrics_list.end()) << "Metric list" << key << " is missing.";
  MetricValueList metric_list = it->second;
  MetricValue metric = metric_list.metrics().at(index);
  EXPECT_EQ(metric.value(), expected_value)
      << "Unexpected value for metric " << key;
  if (!expected_partition.empty()) {
    EXPECT_EQ(metric.partition(), expected_partition)
        << "Unexpected partition for metric " << key;
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // PRIVACY_SANDBOX_BIDDING_AUCTION_SERVERS_TEST_UTIL_H_
