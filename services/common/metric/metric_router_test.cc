//  Copyright 2022 Google LLC
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless_ required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express_ or implied.
//  See the License for the specific language governing permiss_ions and
//  limitations under the License.

#include "services/common/metric/metric_router.h"

#include <sstream>
#include <string>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/exporters/ostream/metric_exporter.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader.h"
#include "opentelemetry/sdk/metrics/meter.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

namespace privacy_sandbox::server_common::metric {
namespace {

namespace metric_sdk = ::opentelemetry::sdk::metrics;
namespace metrics_api = ::opentelemetry::metrics;

using ::testing::ContainsRegex;

constexpr int kExportIntervalMillis = 50;

constexpr Definition<int, Privacy::kNonImpacting, Instrument::kUpDownCounter>
    kSafeCounter("safe_counter", "description");
constexpr Definition<double, Privacy::kNonImpacting, Instrument::kUpDownCounter>
    kSafeCounterDouble("safe_double_counter", "description");

constexpr double histogram_boundaries[] = {50, 100, 200};
constexpr Definition<int, Privacy::kNonImpacting, Instrument::kHistogram>
    kSafeHistogram("safe_histogram", "description", histogram_boundaries);
constexpr Definition<double, Privacy::kNonImpacting, Instrument::kHistogram>
    kSafeHistogramDouble("safe_double_histogram", "description",
                         histogram_boundaries);

constexpr absl::string_view buyer_public_partitions[] = {"buyer_1", "buyer_2",
                                                         "buyer_3"};
constexpr Definition<int, Privacy::kNonImpacting,
                     Instrument::kPartitionedCounter>
    kSafePartitioned("safe_partitioned_counter", "description", "buyer_name",
                     buyer_public_partitions);
constexpr Definition<double, Privacy::kNonImpacting,
                     Instrument::kPartitionedCounter>
    kSafePartitionedDouble("safe_partitioned_double_counter", "description",
                           "buyer_name_double", buyer_public_partitions);

class MetricRouterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto provider = std::make_shared<metric_sdk::MeterProvider>();
    provider->AddMetricReader(
        std::make_unique<metric_sdk::PeriodicExportingMetricReader>(
            std::make_unique<
                opentelemetry::exporter::metrics::OStreamMetricExporter>(
                GetSs(), metric_sdk::AggregationTemporality::kDelta),
            metric_sdk::PeriodicExportingMetricReaderOptions{
                /*export_interval_millis*/ std::chrono::milliseconds(
                    kExportIntervalMillis),
                /*export_timeout_millis*/ std::chrono::milliseconds(
                    kExportIntervalMillis / 2)}));
    metrics_api::Provider::SetMeterProvider(
        (std::shared_ptr<metrics_api::MeterProvider>)provider);

    test_instance_ =
        std::make_unique<MetricRouter>(metrics_api::Provider::GetMeterProvider()
                                           ->GetMeter("not used name", "0.0.1")
                                           .get());
  }

  static std::stringstream& GetSs() {
    // never destructed, outlive 'OStreamMetricExporter'
    static auto* ss = new std::stringstream();
    return *ss;
  }
  std::string ReadSs() {
    absl::SleepFor(absl::Milliseconds(kExportIntervalMillis * 2));
    std::string output = GetSs().str();
    GetSs().str("");
    return output;
  }
  std::unique_ptr<MetricRouter> test_instance_;
};

TEST_F(MetricRouterTest, LogSafeInt) {
  CHECK_OK(test_instance_->LogSafe(kSafeCounter, 123, ""));
  std::string output = ReadSs();
  EXPECT_THAT(output,
              ContainsRegex("instrument name[ \t]+:[ \t]+safe_counter"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+123"));
}

TEST_F(MetricRouterTest, LogSafeIntTwice) {
  CHECK_OK(test_instance_->LogSafe(kSafeCounter, 123, ""));
  CHECK_OK(test_instance_->LogSafe(kSafeCounter, 123, ""));
  std::string output = ReadSs();
  EXPECT_THAT(output,
              ContainsRegex("instrument name[ \t]+:[ \t]+safe_counter"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+246"));
}

TEST_F(MetricRouterTest, LogSafeDouble) {
  CHECK_OK(test_instance_->LogSafe(kSafeCounterDouble, 4.56, ""));
  std::string output = ReadSs();
  EXPECT_THAT(output,
              ContainsRegex("instrument name[ \t]+:[ \t]+safe_double_counter"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+4.56"));
}

TEST_F(MetricRouterTest, LogSafeIntHistogram) {
  CHECK_OK(test_instance_->LogSafe(kSafeHistogram, 123, ""));
  std::string output = ReadSs();
  EXPECT_THAT(output,
              ContainsRegex("instrument name[ \t]+:[ \t]+safe_histogram"));
  EXPECT_THAT(output, ContainsRegex("sum[ \t]+:[ \t]+123"));
  EXPECT_THAT(output, ContainsRegex("buckets[ \t]+:[ \t]+[[]50, 100, 200"));
}

TEST_F(MetricRouterTest, LogSafeDoubleHistogram) {
  CHECK_OK(test_instance_->LogSafe(kSafeHistogramDouble, 100.23, ""));
  std::string output = ReadSs();

  EXPECT_THAT(output, ContainsRegex(
                          "instrument name[ \t]+:[ \t]+safe_double_histogram"));
  EXPECT_THAT(output, ContainsRegex("sum[ \t]+:[ \t]+100.23"));
  EXPECT_THAT(output, ContainsRegex("buckets[ \t]+:[ \t]+[[]50, 100, 200"));
}

TEST_F(MetricRouterTest, LogSafeDoubleHistogramTwice) {
  CHECK_OK(test_instance_->LogSafe(kSafeHistogramDouble, 100.11, ""));
  CHECK_OK(test_instance_->LogSafe(kSafeHistogramDouble, 200.22, ""));
  std::string output = ReadSs();

  EXPECT_THAT(output, ContainsRegex(
                          "instrument name[ \t]+:[ \t]+safe_double_histogram"));
  EXPECT_THAT(output, ContainsRegex("sum[ \t]+:[ \t]+300.33"));
  EXPECT_THAT(output, ContainsRegex("buckets[ \t]+:[ \t]+[[]50, 100, 200"));
}

TEST_F(MetricRouterTest, LogTwoMetric) {
  CHECK_OK(test_instance_->LogSafe(kSafeCounter, 123, ""));
  CHECK_OK(test_instance_->LogSafe(kSafeHistogram, 456, ""));
  std::string output = ReadSs();
  EXPECT_THAT(output,
              ContainsRegex("instrument name[ \t]+:[ \t]+safe_counter"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+123"));
  EXPECT_THAT(output,
              ContainsRegex("instrument name[ \t]+:[ \t]+safe_histogram"));
  EXPECT_THAT(output, ContainsRegex("sum[ \t]+:[ \t]+456"));
}

TEST_F(MetricRouterTest, LogSafePartitioned) {
  CHECK_OK(test_instance_->LogSafe(kSafePartitioned, 111, "buyer_1"));
  CHECK_OK(test_instance_->LogSafe(kSafePartitioned, 1000, "buyer_1"));
  CHECK_OK(test_instance_->LogSafe(kSafePartitioned, 22, "buyer_2"));
  std::string output = ReadSs();
  EXPECT_THAT(
      output,
      ContainsRegex("instrument name[ \t]+:[ \t]+safe_partitioned_counter"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+1111"));
  EXPECT_THAT(output, ContainsRegex("buyer_name[ \t]*:[ \t]*buyer_1"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+22"));
  EXPECT_THAT(output, ContainsRegex("buyer_name[ \t]*:[ \t]*buyer_2"));
}

TEST_F(MetricRouterTest, LogSafePartitionedDouble) {
  CHECK_OK(test_instance_->LogSafe(kSafePartitionedDouble, 3.21, "buyer_3"));
  std::string output = ReadSs();
  EXPECT_THAT(
      output,
      ContainsRegex(
          "instrument name[ \t]+:[ \t]+safe_partitioned_double_counter"));
  EXPECT_THAT(output, ContainsRegex("value[ \t]+:[ \t]+3.21"));
  EXPECT_THAT(output, ContainsRegex("buyer_name_double[ \t]*:[ \t]*buyer_3"));
}
}  // namespace
}  // namespace privacy_sandbox::server_common::metric
