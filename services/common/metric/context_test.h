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

#include <algorithm>
#include <memory>
#include <string>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/metric/context.h"

namespace privacy_sandbox::server_common::metric {

using ::testing::_;
using ::testing::A;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Exactly;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::Pair;
using ::testing::Ref;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::StrictMock;

using DefinitionSafe =
    Definition<int, Privacy::kNonImpacting, Instrument::kUpDownCounter>;
using DefinitionUnSafe =
    Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter>;
using DefinitionPartition =
    Definition<int, Privacy::kNonImpacting, Instrument::kPartitionedCounter>;
using DefinitionPartitionUnsafe =
    Definition<int, Privacy::kImpacting, Instrument::kPartitionedCounter>;
using DefinitionHistogram =
    Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>;
using DefinitionGauge =
    Definition<int, Privacy::kNonImpacting, Instrument::kGauge>;

inline constexpr DefinitionSafe kIntExactCounter("kIntExactCounter", "");
inline constexpr DefinitionSafe kIntExactCounter2("kIntExactCounter2", "");
inline constexpr DefinitionUnSafe kIntApproximateCounter(
    "kIntApproximateCounter", "", 0, 1);
inline constexpr DefinitionUnSafe kIntApproximateCounter2(
    "kIntApproximateCounter2", "", 0, 1);

inline constexpr absl::string_view pv[] = {"buyer_1", "buyer_2", "buyer_3",
                                           "buyer_4", "buyer_5", "buyer_6"};
inline constexpr DefinitionPartition kIntExactPartitioned(
    "kIntExactPartitioned", "", "buyer_name", pv);
inline constexpr DefinitionPartitionUnsafe kIntUnSafePartitioned(
    "kIntUnSafePartitioned", "", "buyer_name", 5, pv, 1, 1);
inline constexpr DefinitionPartitionUnsafe kIntUnSafePrivatePartitioned(
    "kIntUnSafePrivatePartitioned", "", "buyer_name", 5, kEmptyPublicPartition,
    1, 1);

inline constexpr double hb[] = {50, 100, 200};
inline constexpr DefinitionHistogram kIntExactHistogram("kIntExactHistogram",
                                                        "", hb);

inline constexpr DefinitionGauge kIntExactGauge("kIntExactGauge", "");

inline constexpr const DefinitionName* metric_list[] = {
    &kIntExactCounter,        &kIntExactCounter2,    &kIntApproximateCounter,
    &kIntApproximateCounter2, &kIntExactPartitioned, &kIntUnSafePartitioned,
    &kIntExactHistogram,      &kIntExactGauge};
inline constexpr absl::Span<const DefinitionName* const> metric_list_span =
    metric_list;
[[maybe_unused]] inline constexpr DefinitionSafe kNotInList("kNotInList", "");

class MockMetricRouter {
 public:
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionSafe&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionUnSafe&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionPartition&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionPartitionUnsafe&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionHistogram&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionGauge&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionUnSafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionSafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionPartition&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionPartitionUnsafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionHistogram&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionGauge&), int, absl::string_view));
};

class BaseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TelemetryConfig config_proto;
    config_proto.set_mode(TelemetryConfig::PROD);
    static BuildDependentConfig metric_config(config_proto);
    context_ = Context<metric_list_span, MockMetricRouter>::GetContext(
        &mock_metric_router_, metric_config);
  }

  StrictMock<MockMetricRouter> mock_metric_router_;
  std::unique_ptr<Context<metric_list_span, MockMetricRouter>> context_;
};

class ContextTest : public BaseTest {
 protected:
  void LogUnSafeForApproximate() {
    EXPECT_CALL(
        mock_metric_router_,
        LogUnSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntApproximateCounter)),
                  Eq(1), _))
        .WillOnce(Return(absl::OkStatus()));
    CHECK_OK(context_->LogMetric<kIntApproximateCounter>(1));

    CHECK_OK(context_->LogMetricDeferred<kIntApproximateCounter2>(
        []() mutable { return 2; }));
    EXPECT_CALL(mock_metric_router_,
                LogUnSafe(Matcher<const DefinitionUnSafe&>(
                              Ref(kIntApproximateCounter2)),
                          Eq(2), _))
        .WillOnce(Return(absl::OkStatus()));
  }

  void ErrorLogSafeAfterDecrypt() {
    EXPECT_EQ(context_->LogMetric<kIntExactCounter>(1).code(),
              absl::StatusCode::kFailedPrecondition);
    EXPECT_EQ(
        context_
            ->LogMetricDeferred<kIntExactCounter2>([]() mutable { return 2; })
            .code(),
        absl::StatusCode::kFailedPrecondition);
  }
};

class MetricConfigTest : public ::testing::Test {
 protected:
  void SetUpWithConfig(const BuildDependentConfig& metric_config) {
    context_ = Context<metric_list_span, MockMetricRouter>::GetContext(
        &mock_metric_router_, metric_config);
  }

  StrictMock<MockMetricRouter> mock_metric_router_;
  std::unique_ptr<Context<metric_list_span, MockMetricRouter>> context_;
};

}  // namespace privacy_sandbox::server_common::metric
