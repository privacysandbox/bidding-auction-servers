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

#include "services/common/metric/definition.h"

#include <vector>

#include "absl/log/absl_log.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::server_common::metric {
namespace {
constexpr double hb[] = {1, 2};
constexpr absl::string_view pv[] = {"buyer_1", "buyer_2"};
constexpr Definition<int, Privacy::kNonImpacting, Instrument::kUpDownCounter>
    c2("c2", "c21");
constexpr Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter> c3(
    "c3", "c31", 123, 234);
constexpr Definition<int, Privacy::kNonImpacting,
                     Instrument::kPartitionedCounter>
    c4("c4", "c41", "buyer_name", pv);
constexpr Definition<int, Privacy::kImpacting, Instrument::kPartitionedCounter>
    c5("c5", "c51", "buyer_name", 2, pv, 123, 111);
constexpr Definition<int, Privacy::kNonImpacting, Instrument::kHistogram> c6(
    "c6", "c61", hb);
constexpr Definition<int, Privacy::kImpacting, Instrument::kHistogram> c7("c7",
                                                                          "c71",
                                                                          hb);
constexpr Definition<int, Privacy::kNonImpacting, Instrument::kGauge> c8(
    "c8 gauge", "c81");

inline constexpr const DefinitionName* kList[] = {&c2, &c3, &c4, &c5,
                                                  &c6, &c7, &c8};
inline constexpr absl::Span<const DefinitionName* const> kSpan = kList;

TEST(Init, AllTypes) {
  static_assert(IsInList(c2, kSpan));
  static_assert(IsInList(c3, kSpan));
}

TEST(MetricList, CopiedValue) {
  EXPECT_EQ(/*c2*/ kSpan.at(0)->privacy_budget_weight_copy_, 0);
  EXPECT_EQ(/*c3*/ kSpan.at(1)->privacy_budget_weight_copy_, 1);
  EXPECT_EQ(/*c4*/ kSpan.at(2)->public_partitions_copy_, pv);
  EXPECT_EQ(/*c5*/ kSpan.at(3)->public_partitions_copy_, pv);
  EXPECT_EQ(/*c6*/ kSpan.at(4)->histogram_boundaries_copy_, hb);
  EXPECT_EQ(/*c7*/ kSpan.at(5)->histogram_boundaries_copy_, hb);
}

}  // namespace
}  // namespace privacy_sandbox::server_common::metric
