// Copyright 2023 Google LLC
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

#include "services/common/util/read_system.h"

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "include/gtest/gtest.h"

namespace privacy_sandbox::server_common {
namespace {

TEST(ReadSystem, GetMemory) {
  ABSL_LOG(INFO) << absl::StrJoin(GetMemory(), " ", absl::PairFormatter("->"));
}

TEST(ReadSystem, ReadCpuTime) {
  EXPECT_THAT(internal::ReadCpuTime({}, {}), testing::FieldsAre(0, 0));
  EXPECT_THAT(internal::ReadCpuTime({1, 1, 1, 1}, {}),
              testing::FieldsAre(0.75, 0));

  constexpr int kUtime = 13, kStime = 14;
  std::vector<std::string> fields(52, "");
  fields[kUtime] = "1";
  fields[kStime] = "0";

  EXPECT_THAT(internal::ReadCpuTime({2, 1, 1, 2}, fields),
              testing::FieldsAre(0.5, 0.5));
  ABSL_LOG(INFO) << absl::StrJoin(GetCpu(), " ", absl::PairFormatter("->"));
}

}  // namespace
}  // namespace privacy_sandbox::server_common
