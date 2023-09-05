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

#include "services/common/metric/context_test.h"

namespace privacy_sandbox::server_common::metric {

TEST(BoundPartitionsContributed, Bound) {
  absl::flat_hash_map<std::string, int> m;
  for (int i = 1; i < 10; ++i) {
    m.emplace(absl::StrCat("buyer_", i), i);
    std::vector<std::pair<std::string, int>> ret1 =
        BoundPartitionsContributed(m, kIntUnSafePartitioned);
    EXPECT_EQ(ret1.size(),
              std::min(kIntUnSafePartitioned.max_partitions_contributed_, i));
  }
}

TEST(BoundPartitionsContributed, Filter) {
  absl::flat_hash_map<std::string, int> m = {{"private partition", 1}};
  EXPECT_THAT(BoundPartitionsContributed(m, kIntUnSafePartitioned), IsEmpty());
  EXPECT_THAT(BoundPartitionsContributed(m, kIntUnSafePrivatePartitioned),
              SizeIs(1));
}

TEST_F(BaseTest, LogUpDownCounter) {
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)),
                      Eq(1), _, _))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->LogUpDownCounter<kIntExactCounter>(1));
  // auto e1 = context_->LogUpDownCounter<kIntExactGauge>(1);  // compile errors
  // auto e2 = context_->LogUpDownCounterDeferred<kIntExactGauge>(
  //     []() mutable { return 2; });  // compile errors
}

TEST_F(BaseTest, LogUpDownCounterDeferred) {
  CHECK_OK(context_->LogUpDownCounterDeferred<kIntExactCounter>(
      []() mutable { return 2; }));
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)),
                      Eq(2), _, _))
      .WillOnce(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogPartitionedCounter) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kIntExactPartitioned)),
              Eq(1), StartsWith("buyer_"), _))
      .Times(Exactly(2))
      .WillRepeatedly(Return(absl::OkStatus()));
  CHECK_OK(context_->LogUpDownCounter<kIntExactPartitioned>(
      {{"buyer_1", 1}, {"buyer_2", 1}}));
  // auto e1 = context_->LogUpDownCounter<kIntExactCounter>(
  //     {{"buyer_1", 1}, {"buyer_2", 1}});  // compile errors
  // auto e2 = context_->LogUpDownCounterDeferred<kIntExactCounter>(
  //     []() -> absl::flat_hash_map<std::string, int> {
  //       return {{"buyer_3", 2}, {"buyer_4", 2}};
  //     });  // compile errors
}

TEST_F(BaseTest, LogPartitionedCounterDeferred) {
  CHECK_OK(context_->LogUpDownCounterDeferred<kIntExactPartitioned>(
      []() -> absl::flat_hash_map<std::string, int> {
        return {{"buyer_3", 2}, {"buyer_4", 2}};
      }));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kIntExactPartitioned)),
              Eq(2), StartsWith("buyer_"), _))
      .Times(Exactly(2))
      .WillRepeatedly(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogHistogram) {
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kIntExactHistogram)),
              Eq(1), _, _))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->LogHistogram<kIntExactHistogram>(1));
  // auto e1 = context_->LogHistogram<kIntExactGauge>(1);  // compile errors
  // auto e2 = context_->LogHistogramDeferred<kIntExactGauge>(
  //     []() mutable { return 2; });  // compile errors
}

TEST_F(BaseTest, LogHistogramDeferred) {
  CHECK_OK(context_->LogHistogramDeferred<kIntExactHistogram>(
      []() mutable { return 2; }));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kIntExactHistogram)),
              Eq(2), _, _))
      .WillOnce(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogGauge) {
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionGauge&>(Ref(kIntExactGauge)),
                      Eq(1), _, _))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->LogGauge<kIntExactGauge>(1));
  // auto e1 = context_->LogGauge<kIntExactCounter>(1);  // compile errors
  // auto e2 = context_->LogGaugeDeferred<kIntExactCounter>(
  //     []() mutable { return 2; });  // compile errors
}

TEST_F(BaseTest, LogGaugeDeferred) {
  CHECK_OK(
      context_->LogGaugeDeferred<kIntExactGauge>([]() mutable { return 2; }));
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionGauge&>(Ref(kIntExactGauge)),
                      Eq(2), _, _))
      .WillOnce(Return(absl::OkStatus()));
}

TEST_F(BaseTest, LogPartitionedFiltered) {
  EXPECT_CALL(mock_metric_router_,
              LogUnSafe(Matcher<const DefinitionPartitionUnsafe&>(
                            Ref(kIntUnSafePartitioned)),
                        Eq(1), StartsWith("buyer_1")))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->LogUpDownCounter<kIntUnSafePartitioned>(
      {{"buyer_1", 1}, {"buyer_100", 1}}));
}

TEST_F(BaseTest, LogPartitionedBounded) {
  absl::flat_hash_map<std::string, int> m;
  for (int i = 1; i < 10; ++i) {
    m.emplace(absl::StrCat("buyer_", i), i);
  }
  EXPECT_CALL(mock_metric_router_,
              LogUnSafe(Matcher<const DefinitionPartitionUnsafe&>(
                            Ref(kIntUnSafePartitioned)),
                        A<int>(), StartsWith("buyer_")))
      .Times(Exactly(kIntUnSafePartitioned.max_partitions_contributed_))
      .WillRepeatedly(Return(absl::OkStatus()));
  CHECK_OK(context_->LogUpDownCounter<kIntUnSafePartitioned>(m));
}

TEST_F(BaseTest, LogPartitionedCounterDeferredBounded) {
  CHECK_OK(context_->LogUpDownCounterDeferred<kIntUnSafePartitioned>([]() {
    absl::flat_hash_map<std::string, int> m;
    for (int i = 1; i < 10; ++i) {
      m.emplace(absl::StrCat("buyer_", i), i);
    }
    return m;
  }));
  EXPECT_CALL(mock_metric_router_,
              LogUnSafe(Matcher<const DefinitionPartitionUnsafe&>(
                            Ref(kIntUnSafePartitioned)),
                        A<int>(), StartsWith("buyer_")))
      .Times(Exactly(kIntUnSafePartitioned.max_partitions_contributed_))
      .WillRepeatedly(Return(absl::OkStatus()));
}

TEST_F(ContextTest, LogBeforeDecrypt) {
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)),
                      Eq(1), _, _))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->LogMetric<kIntExactCounter>(1));
  // auto e1 = context_->LogMetric<kIntExactCounter>(1.2);  // compile errors
  // auto e2 = context_->LogMetric<kNotInList>(1);          // compile errors

  CHECK_OK(context_->LogMetricDeferred<kIntExactCounter2>(
      []() mutable { return 2; }));
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter2)),
                      Eq(2), _, _))
      .WillOnce(Return(absl::OkStatus()));
  // absl::AnyInvocable<int() &&> cb = []() mutable { return 2; };
  // auto e1 = context_->LogMetricDeferred<kIntExactCounter>(cb);  // compile
  // error

  LogUnSafeForApproximate();
}

TEST_F(ContextTest, LogAfterDecrypt) {
  context_->SetDecrypted();
  ErrorLogSafeAfterDecrypt();
  LogUnSafeForApproximate();
}

TEST_F(BaseTest, Accumulate) {
  EXPECT_CALL(
      mock_metric_router_,
      LogUnSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntApproximateCounter)),
                Eq(101), _))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->AccumulateMetric<kIntApproximateCounter>(1));
  CHECK_OK(context_->AccumulateMetric<kIntApproximateCounter>(100));
  // compile errors:
  // CHECK_OK(context_->AccumulateMetric<kIntApproximateCounter>(1.2));
  // CHECK_OK(context_->AccumulateMetric<kNotInList>(1))  ;
  // CHECK_OK(context_->AccumulateMetric<kIntExactCounter>(1));
}

TEST_F(BaseTest, AccumulatePartition) {
  EXPECT_CALL(mock_metric_router_,
              LogUnSafe(Matcher<const DefinitionPartitionUnsafe&>(
                            Ref(kIntUnSafePartitioned)),
                        Eq(101), "buyer_1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_metric_router_,
              LogUnSafe(Matcher<const DefinitionPartitionUnsafe&>(
                            Ref(kIntUnSafePartitioned)),
                        Eq(200), "buyer_2"))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->AccumulateMetric<kIntUnSafePartitioned>(1, "buyer_1"));
  CHECK_OK(context_->AccumulateMetric<kIntUnSafePartitioned>(100, "buyer_1"));
  CHECK_OK(context_->AccumulateMetric<kIntUnSafePartitioned>(200, "buyer_2"));
}

TEST_F(MetricConfigTest, ConfigMetricList) {
  TelemetryConfig config_proto;
  config_proto.set_mode(TelemetryConfig::PROD);
  config_proto.add_metric()->set_name("kIntExactCounter");
  BuildDependentConfig metric_config(config_proto);
  SetUpWithConfig(metric_config);

  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionSafe&>(Ref(kIntExactCounter)),
                      Eq(1), _, _))
      .WillOnce(Return(absl::OkStatus()));
  CHECK_OK(context_->LogUpDownCounter<kIntExactCounter>(1));

  auto s = context_->LogUpDownCounter<kIntExactCounter2>(1);
  EXPECT_TRUE(absl::IsNotFound(s));
}

}  // namespace privacy_sandbox::server_common::metric
