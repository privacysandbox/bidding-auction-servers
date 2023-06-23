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

#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "algorithms/bounded-sum.h"
#include "services/common/metric/definition.h"
#include "services/common/util/status_macros.h"

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
               PrivacyBudget fraction_of_total_budget)
      : metric_router_(metric_router),
        definition_(*definition),
        fraction_of_total_budget_(fraction_of_total_budget) {}

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
        if (!definition_.public_partitions_.empty()) {
          all_partitions = definition_.public_partitions_;
        }
      } else {
        max_partitions_contributed = 1;
      }
      for (absl::string_view each : all_partitions) {
        PS_ASSIGN_OR_RETURN(
            std::unique_ptr<differential_privacy::BoundedSum<TValue>>
                bounded_sum,

            typename differential_privacy::BoundedSum<TValue>::Builder()
                .SetEpsilon(fraction_of_total_budget_.epsilon *
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

  absl::StatusOr<std::vector<differential_privacy::Output>> OutputNoised()
      override ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    std::vector<differential_privacy::Output> ret(bounded_sums_.size());
    auto it = ret.begin();
    for (auto& [partition, bounded_sum] : bounded_sums_) {
      PS_ASSIGN_OR_RETURN(*it, bounded_sum->PartialResult());
      PS_RETURN_IF_ERROR((metric_router_->LogSafe(
          definition_, differential_privacy::GetValue<TValue>(*it),
          partition)));
      ++it;
      bounded_sum->Reset();
    }
    return ret;
  }

 private:
  TMetricRouter* metric_router_;
  const Definition<TValue, privacy, instrument>& definition_;
  PrivacyBudget fraction_of_total_budget_;
  absl::Mutex mutex_;
  absl::flat_hash_map<std::string,
                      std::unique_ptr<differential_privacy::BoundedSum<TValue>>>
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
   `fraction_of_total_budget` is the privacy budget per weight:
   i.e. fraction_of_total_budget = total_budget / total_weight
   used for: privacy_budget = privacy_budget_weight * fraction_of_total_budget;
   `output_period` is the interval to output aggregated and noise result.
   */
  DifferentiallyPrivate(TMetricRouter* metric_router,
                        PrivacyBudget fraction_of_total_budget,
                        absl::Duration output_period)
      : metric_router_(metric_router),
        fraction_of_total_budget_(fraction_of_total_budget),
        output_period_(output_period),
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
      auto it = counter_.find(metric_name);
      if (it == counter_.end()) {
        it = counter_
                 .emplace(metric_name,
                          std::make_unique<CounterT>(metric_router_, definition,
                                                     fraction_of_total_budget_))
                 .first;
      }
      counter = static_cast<CounterT*>(it->second.get());
    }
    return counter->Aggregate(value, partition);
  }

  PrivacyBudget fraction_of_total_budget() const {
    return fraction_of_total_budget_;
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
    for (auto& [name, counter] : counter_) {
      PS_ASSIGN_OR_RETURN(std::vector<differential_privacy::Output> output,
                          counter->OutputNoised());
      ret.emplace(name, std::move(output));
    }
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
  PrivacyBudget fraction_of_total_budget_;
  absl::Duration output_period_;

  absl::Mutex mutex_;
  absl::flat_hash_map<absl::string_view,
                      std::unique_ptr<internal::DpAggregatorBase>>
      counter_ ABSL_GUARDED_BY(mutex_);

  absl::Notification stop_signal;
  std::thread run_output_;
};

}  // namespace privacy_sandbox::server_common::metric

#endif  // SERVICES_COMMON_METRIC_DP_H_
