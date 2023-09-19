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

#include "services/common/metric/dp.h"

#include <future>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "services/common/telemetry/telemetry_flag.h"

namespace privacy_sandbox::server_common::metric {

#define CONCAT_IMPL(x, y) x##y
#define CONCAT_MACRO(x, y) CONCAT_IMPL(x, y)

#define PS_ASSERT_OK_AND_ASSIGN(lhs, rexpr) \
  ASSERT_OK_AND_ASSIGN_IMPL(CONCAT_MACRO(_status_or, __COUNTER__), lhs, rexpr)

#define ASSERT_OK_AND_ASSIGN_IMPL(statusor, lhs, rexpr)     \
  auto statusor = (rexpr);                                  \
  ASSERT_TRUE(statusor.status().ok()) << statusor.status(); \
  lhs = std::move(statusor.value())

using ::testing::_;
using ::testing::A;
using ::testing::AtLeast;
using ::testing::Between;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Matcher;
using ::testing::Pair;
using ::testing::Ref;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;

using DefinitionUnSafe =
    Definition<int, Privacy::kImpacting, Instrument::kUpDownCounter>;
using DefinitionPartition =
    Definition<int, Privacy::kImpacting, Instrument::kPartitionedCounter>;
using DefinitionHistogram =
    Definition<double, Privacy::kImpacting, Instrument::kHistogram>;

constexpr DefinitionUnSafe kIntUnSafeCounter("kIntUnSafeCounter", "", 1, 2);
constexpr DefinitionUnSafe kUnitCounter("kUnitCounter", "", 0, 1);

constexpr absl::string_view pv[] = {"buyer_1", "buyer_2", "buyer_no_data"};
constexpr DefinitionPartition kUnitPartionCounter(
    /*name*/ "kUnitPartionCounter", "", /*partition_type*/ "buyer_name",
    /*max_partitions_contributed*/ 2,
    /*public_partitions*/ pv,
    /*upper_bound*/ 1,
    /*lower_bound*/ 0);

constexpr double kHistogram[] = {50, 100, 250};
constexpr DefinitionHistogram kHistogramCounter("kHistogramCounter", "",
                                                kHistogram, 10000, 0);
constexpr DefinitionHistogram kHistogramBounded("kHistogramBounded", "",
                                                kHistogram, 100, 250);

class MockMetricRouter {
 public:
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionUnSafe&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionPartition&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(absl::Status, LogSafe,
              ((const DefinitionHistogram&), int, absl::string_view,
               (absl::flat_hash_map<std::string, std::string>)));
  MOCK_METHOD(const BuildDependentConfig&, metric_config, ());
};

class NoNoiseTest : public ::testing::Test {
 protected:
  void SetUp() override { InitConfig(60'000); }

  void InitConfig(int dp_export_interval_ms) {
    TelemetryConfig config_proto;
    config_proto.set_dp_export_interval_ms(dp_export_interval_ms);
    metric_config_ = std::make_unique<BuildDependentConfig>(config_proto);
    EXPECT_CALL(mock_metric_router_, metric_config())
        .WillRepeatedly(ReturnRef(*metric_config_));
  }

  std::unique_ptr<BuildDependentConfig> metric_config_;
  virtual PrivacyBudget fraction() { return PrivacyBudget{1e10}; }
  StrictMock<MockMetricRouter> mock_metric_router_;
};

TEST_F(NoNoiseTest, DPCounterReset) {
  internal::DpAggregator d(&mock_metric_router_, &kIntUnSafeCounter,
                           fraction());
  for (int i = 0; i < 10; ++i) {
    CHECK_OK(d.Aggregate(1, ""));
  }
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)),
                      Eq(10), _, _))
      .WillOnce(Return(absl::OkStatus()));
  PS_ASSERT_OK_AND_ASSIGN(auto output, d.OutputNoised());

  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)),
                      Eq(0), _, _))
      .WillOnce(Return(absl::OkStatus()));
  PS_ASSERT_OK_AND_ASSIGN(output, d.OutputNoised());

  CHECK_OK(d.Aggregate(1, ""));
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)),
                      Eq(1), _, _))
      .WillOnce(Return(absl::OkStatus()));
  PS_ASSERT_OK_AND_ASSIGN(output, d.OutputNoised());
}

TEST_F(NoNoiseTest, DPCounterBound) {
  {
    internal::DpAggregator d(&mock_metric_router_, &kIntUnSafeCounter,
                             fraction());
    CHECK_OK(d.Aggregate(5, ""));   // bounded to [1:2]
    CHECK_OK(d.Aggregate(10, ""));  // bounded to [1:2]
    EXPECT_CALL(
        mock_metric_router_,
        LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)), Eq(4),
                _, _))
        .WillOnce(Return(absl::OkStatus()));
    PS_ASSERT_OK_AND_ASSIGN(auto s, d.OutputNoised());
  }
  {
    internal::DpAggregator d(&mock_metric_router_, &kIntUnSafeCounter,
                             fraction());
    CHECK_OK(d.Aggregate(0, ""));  // bounded to [1:2]
    CHECK_OK(d.Aggregate(0, ""));  // bounded to [1:2]
    EXPECT_CALL(
        mock_metric_router_,
        LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)), Eq(2),
                _, _))
        .WillOnce(Return(absl::OkStatus()));
    PS_ASSERT_OK_AND_ASSIGN(auto s, d.OutputNoised());
  }
}

TEST_F(NoNoiseTest, PartitionedCounter) {
  internal::DpAggregator d(&mock_metric_router_, &kUnitPartionCounter,
                           fraction());
  for (int i = 0; i < 10; ++i) {
    CHECK_OK(d.Aggregate(1, "buyer_1"));
  }
  for (int i = 0; i < 20; ++i) {
    CHECK_OK(d.Aggregate(1, "buyer_2"));
  }
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kUnitPartionCounter)),
              Eq(10), Eq("buyer_1"), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kUnitPartionCounter)),
              Eq(20), Eq("buyer_2"), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kUnitPartionCounter)),
              Eq(0), Eq("buyer_no_data"), _))
      .WillOnce(Return(absl::OkStatus()));
  PS_ASSERT_OK_AND_ASSIGN(auto s, d.OutputNoised());
}

TEST_F(NoNoiseTest, HistogramCounter) {
  internal::DpAggregator d(&mock_metric_router_, &kHistogramCounter,
                           fraction());
  for (int i = 0; i < 5; ++i) {
    CHECK_OK(d.Aggregate(i * 100, ""));
  }
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(25), Eq(""), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(75), Eq(""), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(175), Eq(""), _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(251), Eq(""), _))
      .Times(2)
      .WillRepeatedly(Return(absl::OkStatus()));

  PS_ASSERT_OK_AND_ASSIGN(auto s, d.OutputNoised());
}

TEST_F(NoNoiseTest, HistogramBounded) {
  internal::DpAggregator d(&mock_metric_router_, &kHistogramBounded,
                           fraction());
  for (int i = 0; i < 5; ++i) {
    CHECK_OK(d.Aggregate(i * 100, ""));
  }
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramBounded)),
              Eq(75), Eq(""), _))
      .Times(2)
      .WillRepeatedly(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramBounded)),
              Eq(175), Eq(""), _))
      .Times(3)
      .WillRepeatedly(Return(absl::OkStatus()));

  PS_ASSERT_OK_AND_ASSIGN(auto s, d.OutputNoised());
}

class NoiseTest : public NoNoiseTest {
 protected:
  virtual PrivacyBudget fraction() { return PrivacyBudget{1}; }
};

TEST_F(NoiseTest, DPCounterNoise) {
  internal::DpAggregator d(&mock_metric_router_, &kUnitCounter, fraction());
  for (int i = 0; i < 100; ++i) {
    CHECK_OK(d.Aggregate(1, ""));
  }
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kUnitCounter)), A<int>(), _,
              ElementsAre(Pair(kNoiseAttribute, "Noised"))))
      .WillOnce(Return(absl::OkStatus()));
  PS_ASSERT_OK_AND_ASSIGN(std::vector<differential_privacy::Output> s,
                          d.OutputNoised());
  for (const differential_privacy::Output& o : s) {
    EXPECT_THAT(GetNoiseConfidenceInterval(o).upper_bound(),
                DoubleNear(3, 0.1));
  }
}

TEST_F(NoiseTest, DPPartitionCounterNoise) {
  internal::DpAggregator d(&mock_metric_router_, &kUnitPartionCounter,
                           fraction());
  for (int i = 0; i < 100; ++i) {
    CHECK_OK(d.Aggregate(1, "buyer_1"));
    CHECK_OK(d.Aggregate(1, "buyer_2"));
  }
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionPartition&>(Ref(kUnitPartionCounter)),
              A<int>(), _, ElementsAre(Pair(kNoiseAttribute, "Noised"))))
      .WillRepeatedly(Return(absl::OkStatus()));
  PS_ASSERT_OK_AND_ASSIGN(std::vector<differential_privacy::Output> s,
                          d.OutputNoised());
  for (const differential_privacy::Output& o : s) {
    EXPECT_THAT(GetNoiseConfidenceInterval(o).upper_bound(),
                DoubleNear(6, 0.1));
  }
}

TEST_F(NoiseTest, HistogramCounter) {
  internal::DpAggregator d(&mock_metric_router_, &kHistogramCounter,
                           fraction());
  for (int i = 0; i < 100; ++i) {
    CHECK_OK(d.Aggregate(100, ""));
  }
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(75), Eq(""), ElementsAre(Pair(kNoiseAttribute, "Noised"))))
      .Times(AtLeast(50))
      .WillRepeatedly(Return(absl::OkStatus()));

  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(25), Eq(""), _))
      .WillRepeatedly(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(175), Eq(""), _))
      .WillRepeatedly(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionHistogram&>(Ref(kHistogramCounter)),
              Eq(251), Eq(""), _))
      .WillRepeatedly(Return(absl::OkStatus()));

  PS_ASSERT_OK_AND_ASSIGN(auto s, d.OutputNoised());
  for (const differential_privacy::Output& o : s) {
    EXPECT_THAT(GetNoiseConfidenceInterval(o).upper_bound(),
                DoubleNear(3, 0.1));
  }
}

TEST_F(NoNoiseTest, DifferentiallyPrivate) {
  DifferentiallyPrivate dp(&mock_metric_router_, fraction());

  CHECK_OK(dp.Aggregate(&kIntUnSafeCounter, 1, ""));
  CHECK_OK(dp.Aggregate(&kIntUnSafeCounter, 2, ""));
  CHECK_OK(dp.Aggregate(&kUnitCounter, 1, ""));
  CHECK_OK(dp.Aggregate(&kUnitCounter, 2, ""));
  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)),
                      Eq(3), _, _))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_metric_router_,
      LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kUnitCounter)), Eq(2), _, _))
      .WillOnce(Return(absl::OkStatus()));

  PS_ASSERT_OK_AND_ASSIGN(auto s, dp.OutputNoised());

  EXPECT_CALL(mock_metric_router_,
              LogSafe(A<const DefinitionUnSafe&>(), Eq(0), _, _))
      .WillRepeatedly(Return(absl::OkStatus()));
}

class ThreadTest : public NoNoiseTest {
 protected:
  void SetUp() override { InitConfig(1); }
};

// multi thread log to same `DpAggregator`
TEST_F(ThreadTest, DpAggregator) {
  internal::DpAggregator d(&mock_metric_router_, &kIntUnSafeCounter,
                           fraction());
  std::vector<std::future<absl::Status>> f;
  absl::Notification start, done;
  constexpr int kThread = 10, kRepeat = 5;
  for (int t = 0; t < kThread; ++t) {
    f.push_back(std::async(std::launch::async, [&]() -> absl::Status {
      start.WaitForNotification();
      for (int i = 0; i < kRepeat; ++i) {
        PS_RETURN_IF_ERROR(d.Aggregate(1, ""));
      }
      return absl::OkStatus();
    }));
  }
  auto f_read = std::async(std::launch::async, [&]() {
    start.WaitForNotification();
    int ret = 0;
    bool read_after_notify = false;
    while (!done.HasBeenNotified() || !read_after_notify) {
      if (done.HasBeenNotified()) {
        read_after_notify = true;
      }
      auto output = d.OutputNoised();
      CHECK_OK(output);
      for (const differential_privacy::Output& t : *output) {
        ret += differential_privacy::GetValue<int>(t);
      }
      done.WaitForNotificationWithTimeout(absl::Milliseconds(1));
    }
    return ret;
  });

  EXPECT_CALL(mock_metric_router_,
              LogSafe(Matcher<const DefinitionUnSafe&>(Ref(kIntUnSafeCounter)),
                      A<int>(), _, _))
      .WillRepeatedly(Return(absl::OkStatus()));
  start.Notify();
  for (int i = 0; i < f.size(); ++i) {
    EXPECT_EQ(f[i].wait_for(std::chrono::milliseconds(50)),
              std::future_status::ready)
        << "thread_index: " << i;
  }
  done.Notify();
  EXPECT_EQ(f_read.get(), kThread * kRepeat);
}

// multi thread log through `DifferentiallyPrivate`
TEST_F(ThreadTest, DifferentiallyPrivate) {
  constexpr DefinitionUnSafe kUnitCounter2("kUnitCounter2", "", 0, 1);
  constexpr DefinitionUnSafe kUnitCounter3("kUnitCounter3", "", 0, 1);
  const DefinitionUnSafe* kMetricList[] = {&kIntUnSafeCounter, &kUnitCounter,
                                           &kUnitCounter2, &kUnitCounter3};
  absl::Span<const DefinitionUnSafe* const> metric_span = kMetricList;

  DifferentiallyPrivate dp(&mock_metric_router_, fraction());
  std::vector<std::future<absl::Status>> f;
  absl::Notification start;
  constexpr int kRepeat = 50;
  for (int t = 0; t < metric_span.size(); ++t) {
    f.push_back(
        std::async(std::launch::async,
                   [def = metric_span[t], &start, &dp]() -> absl::Status {
                     start.WaitForNotification();
                     for (int i = 0; i < kRepeat; ++i) {
                       PS_RETURN_IF_ERROR(dp.Aggregate(def, 1, ""));
                     }
                     return absl::OkStatus();
                   }));
  }

  EXPECT_CALL(mock_metric_router_,
              LogSafe(A<const DefinitionUnSafe&>(), A<int>(), _, _))
      .WillRepeatedly(Return(absl::OkStatus()));
  start.Notify();
  for (int i = 0; i < f.size(); ++i) {
    EXPECT_EQ(f[i].wait_for(std::chrono::milliseconds(50)),
              std::future_status::ready)
        << "thread_index: " << i;
  }
}
}  // namespace privacy_sandbox::server_common::metric
