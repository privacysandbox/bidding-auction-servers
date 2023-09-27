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

#ifndef SERVICES_COMMON_METRIC_DP_H_
#define SERVICES_COMMON_METRIC_DP_H_

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "algorithms/bounded-sum.h"
#include "services/common/metric/definition.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::server_common::metric {

struct PrivacyBudget {
  double epsilon;
};

namespace internal {
// Abstract class of DpAggregator,  can output noised result.
class DpAggregatorBase {
 public:
  // Output aggregated results with DP noise added.
  virtual absl::StatusOr<std::vector<differential_privacy::Output>>
  OutputNoised() = 0;
  virtual ~DpAggregatorBase() = default;
};

// DpAggregator is thread-safe counter to aggregate metric and add noise;
// It should only be constructed from `DifferentiallyPrivate`.
// see `DifferentiallyPrivate` about `TMetricRouter`;
// see `Definition` about `TValue`, `privacy`, `instrument`;
template <typename TMetricRouter, typename TValue, Privacy privacy,
          Instrument instrument>
class DpAggregator : public DpAggregatorBase {
 public:
  DpAggregator(TMetricRouter* metric_router,
               const Definition<TValue, privacy, instrument>* definition,
               PrivacyBudget privacy_budget_per_weight)
      : metric_router_(metric_router),
        definition_(*definition),
        privacy_budget_per_weight_(privacy_budget_per_weight) {}

  // Aggregate `value` for  a metric. This is only called from
  // `DifferentiallyPrivate`, can be called multiple times before
  // `OutputNoised()` result.  Each `partion` aggregate separately. If not
  // partitioned, `partition` is empty string.
  absl::Status Aggregate(TValue value, absl::string_view partition)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    auto it = bounded_sums_.find(partition);
    if (it == bounded_sums_.end()) {
      int max_partitions_contributed;
      const absl::string_view current_partition[] = {partition};
      absl::Span<const absl::string_view> all_partitions = current_partition;
      if constexpr (instrument == Instrument::kPartitionedCounter) {
        max_partitions_contributed = definition_.max_partitions_contributed_;
        if (absl::Span<const absl::string_view> partitions =
                metric_router_->metric_config().template GetPartition(
                    definition_);
            !partitions.empty()) {
          all_partitions = partitions;
        }
      } else {
        max_partitions_contributed = 1;
      }
      for (absl::string_view each : all_partitions) {
        PS_ASSIGN_OR_RETURN(
            std::unique_ptr<differential_privacy::BoundedSum<TValue>>
                bounded_sum,
            typename differential_privacy::BoundedSum<TValue>::Builder()
                .SetEpsilon(privacy_budget_per_weight_.epsilon *
                            definition_.privacy_budget_weight_)
                .SetLower(definition_.lower_bound_)
                .SetUpper(definition_.upper_bound_)
                .SetMaxPartitionsContributed(max_partitions_contributed)
                .SetLaplaceMechanism(
                    absl::make_unique<
                        differential_privacy::LaplaceMechanism::Builder>())
                .Build());
        it = bounded_sums_.emplace(each, std::move(bounded_sum)).first;
      }
      if constexpr (instrument == Instrument::kPartitionedCounter) {
        it = bounded_sums_.find(partition);
      }
    }
    it->second->AddEntry(value);
    return absl::OkStatus();
  }

  // After each `OutputNoised`, all aggregated value will be reset.
  absl::StatusOr<std::vector<differential_privacy::Output>> OutputNoised()
      override ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    std::vector<differential_privacy::Output> ret(bounded_sums_.size());
    auto it = ret.begin();
    for (auto& [partition, bounded_sum] : bounded_sums_) {
      PS_ASSIGN_OR_RETURN(*it, bounded_sum->PartialResult());
      PS_RETURN_IF_ERROR((metric_router_->LogSafe(
          definition_, differential_privacy::GetValue<TValue>(*it), partition,
          {{kNoiseAttribute.data(), "Noised"}})));
      ++it;
      bounded_sum->Reset();
    }
    return ret;
  }

 private:
  TMetricRouter* metric_router_;
  const Definition<TValue, privacy, instrument>& definition_;
  PrivacyBudget privacy_budget_per_weight_;
  absl::Mutex mutex_;
  absl::flat_hash_map<std::string,
                      std::unique_ptr<differential_privacy::BoundedSum<TValue>>>
      bounded_sums_ ABSL_GUARDED_BY(mutex_);
};

// Get mean value of the boundaries of the bucket, used to log into OTel
// histogram; `index` is the bucket index corresponding to `boundaries`.
// For example, with boundaries [10, 20, 30], the effective buckets are [
// [0,10), [10,20), [20,30), [30, ) ] with index [0, 1, 2, 3],
// BucketMean returns [5, 15, 25, 31] for them. i.e. returns mean for buckets
// within 2 boundaries, +1 for buckets on the end.
inline double BucketMean(int index, absl::Span<const double> boundaries) {
  if (index == 0) {
    return boundaries[0] / 2;
  } else if (index == boundaries.size()) {
    return boundaries.back() + 1;
  } else {
    return (boundaries[index - 1] + boundaries[index]) / 2;
  }
}

// partial specialization of DpAggregator<> for kHistogram
template <typename TMetricRouter, typename TValue, Privacy privacy>
class DpAggregator<TMetricRouter, TValue, privacy, Instrument::kHistogram>
    : public DpAggregatorBase {
 public:
  DpAggregator(
      TMetricRouter* metric_router,
      const Definition<TValue, privacy, Instrument::kHistogram>* definition,
      PrivacyBudget privacy_budget_per_weight)
      : metric_router_(metric_router),
        definition_(*definition),
        privacy_budget_per_weight_(privacy_budget_per_weight) {
    for (int i = 0; i < definition_.histogram_boundaries_.size() + 1; ++i) {
      auto bounded_sum =
          typename differential_privacy::BoundedSum<int>::Builder()
              .SetEpsilon(privacy_budget_per_weight_.epsilon *
                          definition_.privacy_budget_weight_)
              .SetLower(0)
              .SetUpper(1)  // histogram count add at most 1 each time
              .SetMaxPartitionsContributed(1)
              .SetLaplaceMechanism(
                  absl::make_unique<
                      differential_privacy::LaplaceMechanism::Builder>())
              .Build();
      CHECK_OK(bounded_sum);
      bounded_sums_.push_back(*std::move(bounded_sum));
    }
  }

  absl::Status Aggregate(TValue value, absl::string_view partition)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    absl::Span<const double> boundaries = definition_.histogram_boundaries_;
    TValue bounded_value = std::min(std::max(value, definition_.lower_bound_),
                                    definition_.upper_bound_);
    int index =
        std::lower_bound(boundaries.begin(), boundaries.end(), bounded_value) -
        boundaries.begin();
    bounded_sums_[index]->AddEntry(1);
    return absl::OkStatus();
  }

  // Get noised histogram counts in buckets, output them to OTel with an
  // arbitrary bucket value. The bucket value has no impact on the result, as
  // long as it falls into the bucket range. `BucketMean` is used for the value.
  // After each `OutputNoised`, all aggregated value will be reset.
  absl::StatusOr<std::vector<differential_privacy::Output>> OutputNoised()
      override ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    std::vector<differential_privacy::Output> ret(bounded_sums_.size());
    auto it = ret.begin();
    absl::Span<const double> boundaries = definition_.histogram_boundaries_;
    int j = std::lower_bound(boundaries.begin(), boundaries.end(),
                             definition_.lower_bound_) -
            boundaries.begin();
    int upper = std::lower_bound(boundaries.begin(), boundaries.end(),
                                 definition_.upper_bound_) -
                boundaries.begin();
    for (; j <= upper; ++j) {
      auto& bounded_sum = bounded_sums_[j];
      PS_ASSIGN_OR_RETURN(*it, bounded_sum->PartialResult());
      auto bucket_mean =
          (TValue)BucketMean(j, definition_.histogram_boundaries_);
      for (int i = 0, count = differential_privacy::GetValue<int>(*it++);
           i < count; ++i) {
        PS_RETURN_IF_ERROR(
            (metric_router_->LogSafe(definition_, bucket_mean, "",
                                     {{kNoiseAttribute.data(), "Noised"}})));
      }
      bounded_sum->Reset();
    }
    return ret;
  }

 private:
  TMetricRouter* metric_router_;
  const Definition<TValue, privacy, Instrument::kHistogram>& definition_;
  PrivacyBudget privacy_budget_per_weight_;
  absl::Mutex mutex_;
  std::vector<std::unique_ptr<differential_privacy::BoundedSum<int>>>
      bounded_sums_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace internal

/*
`DifferentiallyPrivate` is the thread safe class to aggregate
`Privacy::kImpacting` metrics and output the noised result periodically.

`TMetricRouter` is thread-safe metric_router implementing following method:
  template <typename TValue, Privacy privacy, Instrument instrument>
  absl::Status TMetricRouter::LogSafe(
      const Definition<TValue, privacy, instrument>& definition, TValue value,
      absl::string_view partition,
      absl::flat_hash_map<std::string, std::string> attribute);
*/
template <typename TMetricRouter>
class DifferentiallyPrivate {
 public:
  /*
   privacy_budget_per_weight = total_budget / total_weight
   used for: privacy_budget = privacy_budget_weight * privacy_budget_per_weight;
   `output_period` is the interval to output aggregated and noise result.
   */
  DifferentiallyPrivate(TMetricRouter* metric_router,
                        PrivacyBudget privacy_budget_per_weight)
      : metric_router_(metric_router),
        privacy_budget_per_weight_(privacy_budget_per_weight),
        output_period_(absl::Milliseconds(
            metric_router_->metric_config().dp_export_interval_ms())),
        run_output_(std::thread([this]() { RunOutput(); })) {}

  // Aggregate value for the `definition`.
  // `definition` not owned, must out live `DifferentiallyPrivate`.
  template <typename TValue, Privacy privacy, Instrument instrument>
  absl::Status Aggregate(
      const Definition<TValue, privacy, instrument>* definition, TValue value,
      absl::string_view partition) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::string_view metric_name = definition->name_;
    using CounterT =
        internal::DpAggregator<TMetricRouter, TValue, privacy, instrument>;
    CounterT* counter;
    {
      absl::MutexLock mutex_lock(&mutex_);
      has_data = true;
      auto it = counter_.find(metric_name);
      if (it == counter_.end()) {
        it = counter_
                 .emplace(metric_name, std::make_unique<CounterT>(
                                           metric_router_, definition,
                                           privacy_budget_per_weight_))
                 .first;
      }
      counter = static_cast<CounterT*>(it->second.get());
    }
    return counter->Aggregate(value, partition);
  }

  PrivacyBudget privacy_budget_per_weight() const {
    return privacy_budget_per_weight_;
  }

  ~DifferentiallyPrivate() {
    stop_signal.Notify();
    run_output_.join();
  }

 private:
  friend class NoNoiseTest_DifferentiallyPrivate_Test;

  // Output aggregated results with DP noise added for all defintions with
  // logged metric.
  absl::StatusOr<absl::flat_hash_map<absl::string_view,
                                     std::vector<differential_privacy::Output>>>
  OutputNoised() ABSL_LOCKS_EXCLUDED(mutex_) {
    // ToDo(b/279955396): lock telemetry export when OutputNoised runs
    absl::MutexLock mutex_lock(&mutex_);
    absl::flat_hash_map<absl::string_view,
                        std::vector<differential_privacy::Output>>
        ret;
    if (!has_data) {
      return ret;
    }
    for (auto& [name, counter] : counter_) {
      PS_ASSIGN_OR_RETURN(std::vector<differential_privacy::Output> output,
                          counter->OutputNoised());
      ret.emplace(name, std::move(output));
    }
    has_data = false;
    return ret;
  }

  // Periodically output noised result
  void RunOutput() {
    while (true) {
      stop_signal.WaitForNotificationWithTimeout(output_period_);
      auto result = OutputNoised();
      ABSL_LOG_IF(ERROR, !result.ok()) << result.status();
      if (stop_signal.HasBeenNotified()) {
        break;
      }
    }
  }

  TMetricRouter* metric_router_;
  PrivacyBudget privacy_budget_per_weight_;
  absl::Duration output_period_;

  absl::Mutex mutex_;
  absl::flat_hash_map<absl::string_view,
                      std::unique_ptr<internal::DpAggregatorBase>>
      counter_ ABSL_GUARDED_BY(mutex_);

  absl::Notification stop_signal;
  std::thread run_output_;
  // Only output to OTel if data has been aggregated (`has_data` = true)
  // become true when data has been aggregated, become false after output.
  bool has_data ABSL_GUARDED_BY(mutex_) = false;
};

}  // namespace privacy_sandbox::server_common::metric

#endif  // SERVICES_COMMON_METRIC_DP_H_
