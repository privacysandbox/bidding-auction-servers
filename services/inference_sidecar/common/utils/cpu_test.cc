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

#include "utils/cpu.h"

#include <sched.h>
#include <unistd.h>

#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "googletest/include/gtest/gtest.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

constexpr int kMinCpuId = 0;
constexpr int kNonExistentCpuId = 99999;

TEST(SetCpuAffinity, Success) {
  EXPECT_EQ(SetCpuAffinity({kMinCpuId}).code(), absl::StatusCode::kOk);
}

TEST(SetCpuAffinity, EmptyCpuSet) {
  auto status = SetCpuAffinity({});
  ABSL_LOG(INFO) << status.message();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SetCpuAffinity, NoCpuExists) {
  auto status = SetCpuAffinity({kNonExistentCpuId});
  ABSL_LOG(INFO) << status.message();
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SetCpuAffinity, CpuExistsAfterFiltering) {
  auto status = SetCpuAffinity({kMinCpuId, kNonExistentCpuId});
  ABSL_LOG(INFO) << status.message();
  EXPECT_EQ(status.code(), absl::StatusCode::kOk);
}

TEST(SetCpuAffinity, SetAllCpus) {
  const int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
  std::vector<int> all_cpus;
  for (int i = 0; i < num_cpus; i++) {
    all_cpus.push_back(i);
  }
  auto status = SetCpuAffinity(all_cpus);
  ABSL_LOG(INFO) << status.message();
  EXPECT_EQ(status.code(), absl::StatusCode::kOk);
}

}  // namespace
}  // namespace privacy_sandbox::bidding_auction_servers::inference
