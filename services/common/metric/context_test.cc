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

#include "services/common/metric/context.h"

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace privacy_sandbox::server_common::metric {

using ::testing::_;
using ::testing::Eq;
using ::testing::Matcher;
using ::testing::Ref;
using ::testing::Return;
using ::testing::StartsWith;
using ::testing::StrictMock;

using DefinitionSafe =
    Definition<int, Privacy::kNonImpacting, Instrument::kUpDownCounter>;
using DefinitionUnSafe =
    Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter>;
using DefinitionPartition =
    Definition<int, Privacy::kNonImpacting, Instrument::kPartitionedCounter>;
using DefinitionHistogram =
    Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>;
using DefinitionGauge =
    Definition<int, Privacy::kNonImpacting, Instrument::kGauge>;

constexpr DefinitionSafe kIntExactCounter("kIntExactCounter", "description");
constexpr DefinitionUnSafe kIntApproximateCounter("kIntApproximateCounter",
                                                  "description", 0, 1);

constexpr absl::string_view pv[] = {"buyer_1", "buyer_2"};
constexpr DefinitionPartition kIntExactPartitioned("kIntExactPartitioned",
                                                   "description", "buyer_name",
                                                   pv);

constexpr double hb[] = {50, 100, 200};
constexpr DefinitionHistogram kIntExactHistogram("kIntExactHistogram",
                                                 "description", hb);

constexpr DefinitionGauge kIntExactGauge("kIntExactGauge", "description");

constexpr const DefinitionName* metric_list[] = {
    &kIntExactCounter, &kIntApproximateCounter, &kIntExactPartitioned,
    &kIntExactHistogram, &kIntExactGauge};
constexpr absl::Span<const DefinitionName* const> metric_list_span =
    metric_list;
[[maybe_unused]] constexpr DefinitionSafe kNotInList("kNotInList",
                                                     "description");

class MockMetricRouter {
 public:
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionSafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionUnSafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionPartition&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionHistogram&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionGauge&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionUnSafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionSafe&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionPartition&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionHistogram&), int, absl::string_view));
  MOCK_METHOD(absl::Status, LogUnSafe,
              ((const DefinitionGauge&), int, absl::string_view));
};

class BaseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ServerConfig config_proto;
    config_proto.set_mode(ServerConfig::PROD);
    static BuildDependentConfig metric_config(config_proto);
    context_ = Context<metric_list_span, MockMetricRouter>::GetContext(
        &mock_metric_router_, metric_config);
  }

  StrictMock<MockMetricRouter> mock_metric_router_;
  std::unique_ptr<Context<metric_list_span, MockMetricRouter>> context_;
};

TEST_F(BaseTest, LogUpDownCounter) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)), Eq(1), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(context_->LogUpDownCounter<kIntExactCounter>(1).ok());
  // auto e1 = context_->LogUpDownCounter<kIntExactGauge>(1);  // compile errors
  // auto e2 = context_->LogUpDownCounterDeferred<kIntExactGauge>(
  //     []() mutable { return 2; });  // compile errors

  EXPECT_TRUE((context_
                   ->LogUpDownCounterDeferred<kIntExactCounter>(
                       []() mutable { return 2; })
                   .ok()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)), Eq(2), _))
      .WillOnce(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogPartitionedCounter) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kIntExactPartitioned)),
              Eq(1), StartsWith("buyer_")))
      .WillRepeatedly(Return(absl::OkStatus()));
  EXPECT_TRUE(context_
                  ->LogUpDownCounter<kIntExactPartitioned>(
                      {{"buyer_1", 1}, {"buyer_2", 1}})
                  .ok());
  // auto e1 = context_->LogUpDownCounter<kIntExactCounter>(
  //     {{"buyer_1", 1}, {"buyer_2", 1}});  // compile errors
  // auto e2 = context_->LogUpDownCounterDeferred<kIntExactCounter>(
  //     []() -> absl::flat_hash_map<std::string, int> {
  //       return {{"buyer_3", 2}, {"buyer_4", 2}};
  //     });  // compile errors

  EXPECT_TRUE(context_
                  ->LogUpDownCounterDeferred<kIntExactPartitioned>(
                      []() -> absl::flat_hash_map<std::string, int> {
                        return {{"buyer_3", 2}, {"buyer_4", 2}};
                      })
                  .ok());
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kIntExactPartitioned)),
              Eq(2), StartsWith("buyer_")))
      .WillRepeatedly(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogHistogram) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kIntExactHistogram)),
              Eq(1), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(context_->LogHistogram<kIntExactHistogram>(1).ok());
  // auto e1 = context_->LogHistogram<kIntExactGauge>(1);  // compile errors
  // auto e2 = context_->LogHistogramDeferred<kIntExactGauge>(
  //     []() mutable { return 2; });  // compile errors

  EXPECT_TRUE((
      context_
          ->LogHistogramDeferred<kIntExactHistogram>([]() mutable { return 2; })
          .ok()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kIntExactHistogram)),
              Eq(2), _))
      .WillOnce(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogGauge) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionGauge&>(Ref(kIntExactGauge)), Eq(1), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(context_->LogGauge<kIntExactGauge>(1).ok());
  // auto e1 = context_->LogGauge<kIntExactCounter>(1);  // compile errors
  // auto e2 = context_->LogGaugeDeferred<kIntExactCounter>(
  //     []() mutable { return 2; });  // compile errors

  EXPECT_TRUE(
      (context_->LogGaugeDeferred<kIntExactGauge>([]() mutable { return 2; })
           .ok()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionGauge&>(Ref(kIntExactGauge)), Eq(2), _))
      .WillOnce(Return(absl::OkStatus()));
}

class ContextTest : public BaseTest {
 protected:
  void LogUnSafeForApproximate() {
    EXPECT_CALL(
        mock_metric_router_,
        LogUnSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntApproximateCounter)),
                  Eq(1), _))
        .WillOnce(Return(absl::OkStatus()));
    EXPECT_TRUE(context_->LogMetric<kIntApproximateCounter>(1).ok());

    EXPECT_TRUE(context_
                    ->LogMetricDeferred<kIntApproximateCounter>(
                        []() mutable { return 2; })
                    .ok());
    EXPECT_CALL(
        mock_metric_router_,
        LogUnSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntApproximateCounter)),
                  Eq(2), _))
        .WillOnce(Return(absl::OkStatus()));
  }
};

TEST_F(ContextTest, LogBeforeDecrypt) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)), Eq(1), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(context_->LogMetric<kIntExactCounter>(1).ok());
  // auto e1 = context_->LogMetric<kIntExactCounter>(1.2);  // compile errors
  // auto e2 = context_->LogMetric<kNotInList>(1);          // compile errors

  EXPECT_TRUE(
      (context_->LogMetricDeferred<kIntExactCounter>([]() mutable { return 2; })
           .ok()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)), Eq(2), _))
      .WillOnce(Return(absl::OkStatus()));
  // absl::AnyInvocable<int() &&> cb = []() mutable { return 2; };
  // auto e1 = context_->LogMetricDeferred<kIntExactCounter>(cb);  // compile
  // error

  LogUnSafeForApproximate();
}

TEST_F(ContextTest, LogAfterDecrypt) {
  context_->SetDecrypted();
  EXPECT_EQ(context_->LogMetric<kIntExactCounter>(1).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(
      context_->LogMetricDeferred<kIntExactCounter>([]() mutable { return 2; })
          .code(),
      absl::StatusCode::kFailedPrecondition);

  LogUnSafeForApproximate();
}

TEST_F(ContextTest, LogPartition) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kIntExactPartitioned)),
              Eq(1), StartsWith("buyer_")))
      .WillRepeatedly(Return(absl::OkStatus()));
  EXPECT_TRUE(
      context_
          ->LogMetric<kIntExactPartitioned>({{"buyer_1", 1}, {"buyer_2", 1}})
          .ok());

  EXPECT_TRUE(context_
                  ->LogMetricDeferred<kIntExactPartitioned>(
                      []() -> absl::flat_hash_map<std::string, int> {
                        return {{"buyer_3", 2}, {"buyer_4", 2}};
                      })
                  .ok());
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kIntExactPartitioned)),
              Eq(2), StartsWith("buyer_")))
      .WillRepeatedly(Return(absl::OkStatus()));
}

}  // namespace privacy_sandbox::server_common::metric
