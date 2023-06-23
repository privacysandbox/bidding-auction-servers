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

#ifndef SERVICES_COMMON_METRIC_METRIC_ROUTER_H_
#define SERVICES_COMMON_METRIC_METRIC_ROUTER_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "opentelemetry/metrics/meter.h"
#include "services/common/metric/definition.h"

namespace privacy_sandbox::server_common::metric {

// `MetricRouter` should only be used by `Context`. It provides the api to
// process metric values categorized as safe/unsafe. It handles metric data flow
// into OTel, and DP aggregation.
class MetricRouter {
 public:
  using Meter = ::opentelemetry::metrics::Meter;

  explicit MetricRouter(Meter* meter);

  ~MetricRouter() = default;

  // MetricRouter is neither copyable nor movable
  MetricRouter(const MetricRouter&) = delete;
  MetricRouter& operator=(const MetricRouter&) = delete;

  // For non-partitioned metrics, `partition` be an empty string and not used.
  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogSafe(const Definition<T, privacy, instrument>& definition,
                       T value, absl::string_view partition);

  // For non-partitioned metrics, `partition` be an empty string and not used.
  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogUnSafe(const Definition<T, privacy, instrument>& definition,
                         T value, absl::string_view partition) {
    return absl::UnimplementedError("unsafe not done");
  }

  const Meter* meter() const { return meter_; }

 private:
  void AddHistogramView(absl::string_view instrument_name,
                        const internal::Histogram& histogram);

  template <typename T>
  auto* GetHistogramInstrument(absl::string_view metric_name, T value,
                               const internal::Histogram& histogram);

  template <typename T>
  auto* GetCounterInstrument(absl::string_view metric_name, T value);

  template <typename T>
  T* GetInstrument(absl::string_view metric_name,
                   absl::AnyInvocable<std::unique_ptr<T>() &&> create_new);

  absl::Mutex mutex_;
  absl::flat_hash_map<
      std::string,
      std::unique_ptr<opentelemetry::metrics::SynchronousInstrument>>
      instrument_ ABSL_GUARDED_BY(mutex_);
  Meter* meter_;
};

// This is used to make compile error in certain condition.
template <typename>
inline constexpr bool dependent_false_v = false;

template <typename T>
auto* MetricRouter::GetHistogramInstrument(
    absl::string_view metric_name, T value,
    const internal::Histogram& histogram) {
  namespace api = ::opentelemetry::metrics;
  if constexpr (std::is_same_v<int, T>) {
    using U = api::Histogram<uint64_t>;
    return GetInstrument<U>(metric_name, [metric_name, this, &histogram]() {
      AddHistogramView(metric_name, histogram);
      return std::unique_ptr<U>(
          meter_->CreateUInt64Histogram(metric_name.data()));
    });
  } else if constexpr (std::is_same_v<double, T>) {
    using U = api::Histogram<double>;
    return GetInstrument<U>(metric_name, [metric_name, this, &histogram]() {
      AddHistogramView(metric_name, histogram);
      return std::unique_ptr<U>(
          meter_->CreateDoubleHistogram(metric_name.data()));
    });
  } else {
    static_assert(dependent_false_v<T>);
  }
}

template <typename T>
auto* MetricRouter::GetCounterInstrument(absl::string_view metric_name,
                                         T value) {
  namespace api = ::opentelemetry::metrics;
  if constexpr (std::is_same_v<int, T>) {
    using U = api::UpDownCounter<int64_t>;
    return GetInstrument<U>(metric_name, [metric_name, this]() {
      return std::unique_ptr<U>(
          meter_->CreateInt64UpDownCounter(metric_name.data()));
    });
  } else if constexpr (std::is_same_v<double, T>) {
    using U = api::UpDownCounter<double>;
    return GetInstrument<U>(metric_name, [metric_name, this]() {
      return std::unique_ptr<U>(
          meter_->CreateDoubleUpDownCounter(metric_name.data()));
    });
  } else {
    static_assert(dependent_false_v<T>);
  }
}

template <typename T>
T* MetricRouter::GetInstrument(
    absl::string_view metric_name,
    absl::AnyInvocable<std::unique_ptr<T>() &&> create_new)
    ABSL_LOCKS_EXCLUDED(mutex_) {
  absl::MutexLock mutex_lock(&mutex_);
  auto it = instrument_.find(metric_name);
  if (it == instrument_.end()) {
    it = instrument_.emplace(metric_name, std::move(create_new)()).first;
  }
  return static_cast<T*>(it->second.get());
}

template <typename T, Privacy privacy, Instrument instrument>
absl::Status MetricRouter::LogSafe(
    const Definition<T, privacy, instrument>& definition, T value,
    absl::string_view partition) {
  absl::string_view metric_name = definition.name_;
  if constexpr (instrument == Instrument::kHistogram) {
    GetHistogramInstrument(metric_name, value, definition)
        ->Record(value, opentelemetry::context::Context());
  } else if constexpr (instrument == Instrument::kUpDownCounter) {
    GetCounterInstrument(metric_name, value)->Add(value);
  } else if constexpr (instrument == Instrument::kPartitionedCounter) {
    GetCounterInstrument(metric_name, value)
        ->Add(value, {{definition.partition_type_.data(), partition.data()}});
  } else if constexpr (instrument == Instrument::kGauge) {
    return absl::UnimplementedError("gauge not done");
  } else {
    static_assert(dependent_false_v<T>);
  }
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::server_common::metric

#endif  // SERVICES_COMMON_METRIC_METRIC_ROUTER_H_
