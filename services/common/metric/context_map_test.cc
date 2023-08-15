// Copyright 2023 Google LLC
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

#include "services/common/metric/context_map.h"

#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::server_common::metric {
namespace {

using ::testing::HasSubstr;

constexpr Definition<int, Privacy::kNonImpacting, Instrument::kUpDownCounter>
    kIntExactCounter("kIntExactCounter", "description");
constexpr const DefinitionName* metric_list[] = {&kIntExactCounter};
constexpr absl::Span<const DefinitionName* const> metric_list_span =
    metric_list;

class TestMetricRouter {
 public:
  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogSafe(T value,
                       const Definition<T, privacy, instrument>& definition,
                       absl::string_view partition) {
    return absl::OkStatus();
  }

  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogUnSafe(T value,
                         const Definition<T, privacy, instrument>& definition,
                         absl::string_view partition) {
    return absl::OkStatus();
  }
};

class ContextMapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TelemetryConfig config_proto;
    config_proto.set_mode(TelemetryConfig::PROD);
    metric_config_ = std::make_unique<BuildDependentConfig>(config_proto);
  }
  std::unique_ptr<BuildDependentConfig> metric_config_;
};

class Foo {};

TEST_F(ContextMapTest, GetContext) {
  using TestContextMap = ContextMap<Foo, metric_list_span, TestMetricRouter>;
  Foo foo;
  TestContextMap context_map(std::make_unique<TestMetricRouter>(),
                             *metric_config_);
  EXPECT_FALSE(context_map.Get(&foo).is_decrypted());

  context_map.Get(&foo).SetDecrypted();
  EXPECT_TRUE(context_map.Get(&foo).is_decrypted());
  CHECK_OK(context_map.Remove(&foo));

  EXPECT_FALSE(context_map.Get(&foo).is_decrypted());
}

constexpr absl::string_view pv[] = {"buyer_2", "buyer_1"};
constexpr Definition<int, Privacy::kNonImpacting,
                     Instrument::kPartitionedCounter>
    kIntExactPartitioned("kPartitioned", "description", "buyer_name", pv);
constexpr const DefinitionName* wrong_order_partitioned[] = {
    &kIntExactPartitioned};
constexpr absl::Span<const DefinitionName* const> wrong_order_partitioned_span =
    wrong_order_partitioned;

TEST_F(ContextMapTest, CheckListOrderPartition) {
  EXPECT_DEATH((ContextMap<Foo, wrong_order_partitioned_span, TestMetricRouter>(
                   std::make_unique<TestMetricRouter>(), *metric_config_)),
               HasSubstr("kPartitioned public partitions"));
}

constexpr double hb[] = {50, 100, 200, 1};
constexpr Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>
    kIntExactHistogram("kHistogram", "description", hb);
constexpr const DefinitionName* wrong_order_histogram[] = {&kIntExactHistogram};
constexpr absl::Span<const DefinitionName* const> wrong_order_histogram_span =
    wrong_order_histogram;

TEST_F(ContextMapTest, CheckListOrderHistogram) {
  EXPECT_DEATH((ContextMap<Foo, wrong_order_histogram_span, TestMetricRouter>(
                   std::make_unique<TestMetricRouter>(), *metric_config_)),
               HasSubstr("kHistogram histogram"));
}

constexpr Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter>
    kUnsafe1("kUnsafe1", "", 0, 0);
constexpr Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter>
    kUnsafe2("kUnsafe2", "", 0, 0);
constexpr const DefinitionName* unsafe_list[] = {&kUnsafe1, &kUnsafe2,
                                                 &kIntExactCounter};
constexpr absl::Span<const DefinitionName* const> unsafe_list_span =
    unsafe_list;

TEST_F(ContextMapTest, GetContextMapPrivacyBudget) {
  constexpr server_common::metric::PrivacyBudget budget{/*epsilon*/ 5};
  auto c =
      GetContextMap<Foo, unsafe_list_span>(*metric_config_, nullptr, budget);
  EXPECT_DOUBLE_EQ(c->metric_router()->dp().fraction_of_total_budget().epsilon,
                   2.5);
}

}  // namespace
}  // namespace privacy_sandbox::server_common::metric
