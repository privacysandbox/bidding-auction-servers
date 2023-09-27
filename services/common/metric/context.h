/*
 * Copyright 2023 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_COMMON_METRIC_CONTEXT_H_
#define SERVICES_COMMON_METRIC_CONTEXT_H_

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "services/common/metric/definition.h"
#include "services/common/telemetry/telemetry_flag.h"
#include "src/cpp/util/status_macro/status_macros.h"

namespace privacy_sandbox::server_common::metric {

/*
One context will be created for one request, used to log metric. It will use
`U* metric_router_` to `LogSafe()` only if metric is defined as safe and
request is not decrypted.

To log metric call one of the `Log***<definition>(value)`, definition is a
definition that is in `L`.

Examples:
LogUpDownCounter<kSafeCounter>(1);
LogUpDownCounter<kSafePartitionedCounter>({{"buyer_1", 1}, {"buyer_2", 1}});

To initialize, `L` is the list of Definition* that can be logged. `U` is the
thread-safe metric_router implementing following 2 templated methods:
  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogSafe(const Definition<T, privacy, instrument>& definition,
                       T value,
                       absl::string_view partition);
  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogUnSafe(const Definition<T, privacy, instrument>& definition,
                         T value,
                         absl::string_view partition);
*/
template <const absl::Span<const DefinitionName* const>& L, typename U>
class Context {
 public:
  // Constructed here only, `is_debug`=true will log everything as safe.
  static std::unique_ptr<Context> GetContext(U* metric_router) {
    return absl::WrapUnique(new Context(metric_router));
  }

  // Move only
  Context(Context&&) = default;
  Context& operator=(Context&&) = default;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  ~Context() {
    for (auto& callback : callbacks_) {
      absl::Status s = std::move(callback)();
      ABSL_LOG_IF(ERROR, !s.ok()) << s;
    }

    for (auto& [def, accumulator] : accumulated_metric_) {
      absl::Status s = std::move(accumulator.callback)(accumulator.values);
      ABSL_LOG_IF(ERROR, !s.ok()) << s;
    }
  }

  // Sets once request is decrypted
  void SetDecrypted() { decrypted_ = true; }

  bool is_decrypted() { return decrypted_; }

  // Set once request is successful
  void SetRequestSuccessful() { is_request_successful_ = true; }

  bool is_request_successful() const { return is_request_successful_; }

  // Logs metric for a UpDownCounter
  // Example: LogUpDownCounter<kCounterDefinition>(1);
  template <const auto& definition, typename T>
  absl::Status LogUpDownCounter(
      T value, std::enable_if_t<std::is_arithmetic_v<T>>* = nullptr) {
    static_assert(definition.type_instrument == Instrument::kUpDownCounter);
    return LogMetric<definition>(value);
  }

  // Logs metric for a partitioned UpDownCounter
  // Example: LogUpDownCounter<kPartitionCounterDefinition>({{"buyer", 1}});
  template <const auto& definition,
            typename T = typename std::remove_cv_t<
                std::remove_reference_t<decltype(definition)>>::TypeT>
  absl::Status LogUpDownCounter(
      const absl::flat_hash_map<std::string, T>& value) {
    static_assert(definition.type_instrument ==
                  Instrument::kPartitionedCounter);
    return LogMetric<definition>(value);
  }

  // Logs metric for a Histogram
  // Example: LogHistogram<kHistogramDefinition>(1);
  template <const auto& definition, typename T>
  absl::Status LogHistogram(T value) {
    static_assert(definition.type_instrument == Instrument::kHistogram);
    return LogMetric<definition>(value);
  }

  // Logs metric for a Gauge
  // Example: LogGauge<kGaugeDefinition>(1);
  template <const auto& definition, typename T>
  absl::Status LogGauge(T value) {
    static_assert(definition.type_instrument == Instrument::kGauge);
    return LogMetric<definition>(value);
  }

  // same as `LogUpDownCounter`, but a callback is used to get value at
  // destruction
  template <const auto& definition, typename T>
  absl::Status LogUpDownCounterDeferred(
      T&& callback,
      std::enable_if_t<!std::is_lvalue_reference_v<T>>* = nullptr) {
    static_assert(definition.type_instrument == Instrument::kUpDownCounter ||
                  definition.type_instrument ==
                      Instrument::kPartitionedCounter);
    return LogMetricDeferred<definition>(std::forward<T>(callback));
  }

  // same as `LogHistogram`, but a callback is used to get value at
  // destruction
  template <const auto& definition, typename T>
  absl::Status LogHistogramDeferred(
      T&& callback,
      std::enable_if_t<std::is_arithmetic_v<std::invoke_result_t<T>>>* =
          nullptr,
      std::enable_if_t<!std::is_lvalue_reference_v<T>>* = nullptr) {
    static_assert(definition.type_instrument == Instrument::kHistogram);
    return LogMetricDeferred<definition>(std::forward<T>(callback));
  }

  // same as `LogGauge`, but a callback is used to get value at destruction
  template <const auto& definition, typename T>
  absl::Status LogGaugeDeferred(
      T&& callback,
      std::enable_if_t<std::is_arithmetic_v<std::invoke_result_t<T>>>* =
          nullptr,
      std::enable_if_t<!std::is_lvalue_reference_v<T>>* = nullptr) {
    static_assert(definition.type_instrument == Instrument::kGauge);
    return LogMetricDeferred<definition>(std::forward<T>(callback));
  }

  // Accumulate metric values,  they can be accumulated multiple times during
  // context life time. They will be aggregated and logged at destruction.
  // Metrics must be Privacy::kImpacting.
  template <const auto& definition, typename T>
  absl::Status AccumulateMetric(
      T value, absl::string_view partition = "",
      std::enable_if_t<std::is_arithmetic_v<T>>* = nullptr) {
    CheckDefinition<definition, T>();
    // TODO(b/291336238): Uncomment this static check when marking initiated
    // requests unsafe. static_assert(definition.type_privacy ==
    // Privacy::kImpacting);
    absl::MutexLock mutex_lock(&mutex_);
    auto it = accumulated_metric_.find(&definition);
    if (it != accumulated_metric_.end()) {
      it->second.values[partition] += value;
      return absl::OkStatus();
    }
    accumulated_metric_.emplace(
        &definition,
        Accumulator{
            typename Accumulator::PartitionedValue({{partition.data(), value}}),
            [this](const typename Accumulator::PartitionedValue& values)
                -> absl::Status {
              for (auto& [partition, numeric] :
                   BoundPartitionsContributed(values, definition)) {
                PS_RETURN_IF_ERROR(LogMetricInternal(static_cast<T>(numeric),
                                                     definition, partition));
              }
              return absl::OkStatus();
            }});
    return absl::OkStatus();
  }

 private:
  friend class BaseTest;
  friend class ContextTest;
  friend class ContextTest_LogBeforeDecrypt_Test;
  friend class ContextTest_LogAfterDecrypt_Test;
  friend class ExperimentTest_LogAfterDecrypt_Test;

  explicit Context(U* metric_router) : metric_router_(metric_router) {}

  template <Privacy privacy>
  absl::StatusOr<bool> ShouldLogSafe() {
    if (!metric_router_->metric_config().MetricAllowed()) {
      return absl::PermissionDeniedError("metric is OFF");
    }
    if (decrypted_ && privacy == Privacy::kNonImpacting)
      return absl::FailedPreconditionError(
          "cannot log safe after request being decrypted");
    if (metric_router_->metric_config().IsDebug()) return true;
    return !decrypted_ && privacy == Privacy::kNonImpacting;
  }

  absl::Status AssertLoggable(const DefinitionName& definition)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    if (!logged_metric_.insert(&definition).second) {
      return absl::AlreadyExistsError(
          absl::StrCat(definition.name_, " can only log once for a request."));
    }
    return metric_router_->metric_config()
        .GetMetricConfig(definition.name_)
        .status();
  }

  template <const auto& definition, typename T>
  void CheckDefinition() {
    using DefinitionType =
        std::remove_cv_t<std::remove_reference_t<decltype(definition)>>;
    static_assert(std::is_same_v<typename DefinitionType::TypeT, T>,
                  "value type does not match Metric Defintion");
    static_assert(
        std::is_same_v<DefinitionType, Definition<T, definition.type_privacy,
                                                  definition.type_instrument>>);
    static_assert(IsInList(definition, L));
  }

  template <const auto& definition, typename T>
  absl::Status LogMetric(T value,
                         std::enable_if_t<std::is_arithmetic_v<T>>* = nullptr) {
    CheckDefinition<definition, T>();
    PS_RETURN_IF_ERROR(AssertLoggable(definition));
    static_assert(definition.type_instrument !=
                  Instrument::kPartitionedCounter);
    return LogMetricInternal(value, definition, "");
  }

  template <const auto& definition,
            typename T = typename std::remove_cv_t<
                std::remove_reference_t<decltype(definition)>>::TypeT>
  absl::Status LogMetric(const absl::flat_hash_map<std::string, T>& value) {
    CheckDefinition<definition, T>();
    PS_RETURN_IF_ERROR(AssertLoggable(definition));
    static_assert(definition.type_instrument ==
                  Instrument::kPartitionedCounter);
    for (auto& [partition, numeric] :
         BoundPartitionsContributed(value, definition)) {
      PS_RETURN_IF_ERROR(LogMetricInternal(numeric, definition, partition));
    }
    return absl::OkStatus();
  }

  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogSafe(const Definition<T, privacy, instrument>& definition,
                       T value, absl::string_view partition) {
    return metric_router_->LogSafe(definition, value, partition,
                                   {{kNoiseAttribute.data(), "Raw"}});
  }

  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogMetricInternal(
      T value, const Definition<T, privacy, instrument>& definition,
      absl::string_view partition) {
    PS_ASSIGN_OR_RETURN(const bool log_safe, ShouldLogSafe<privacy>());
    if (log_safe) {
      PS_RETURN_IF_ERROR(LogSafe(definition, value, partition));
      if (metric_router_->metric_config().MetricMode() !=
          TelemetryConfig::COMPARE) {
        return absl::OkStatus();
      }
    }
    if constexpr (privacy == Privacy::kImpacting) {
      return metric_router_->LogUnSafe(definition, value, partition);
    } else {
      return absl::OkStatus();
    }
  }

  // Same as `LogMetric`, instead providing a value, a callback is used to get
  // value at destruction. Trying to add callback for
  // safe(`privacy`=`kNonImpacting`) metric after "Decrypted" will cause error.
  // `callback` should be rvalue, in format like `absl::AnyInvocable<int() &&>`
  template <const auto& definition, typename T>
  absl::Status LogMetricDeferred(
      T&& callback,
      std::enable_if_t<std::is_arithmetic_v<std::invoke_result_t<T>>>* =
          nullptr,
      std::enable_if_t<!std::is_lvalue_reference_v<T>>* = nullptr) {
    using Result = std::invoke_result_t<T>;
    CheckDefinition<definition, Result>();
    PS_RETURN_IF_ERROR(AssertLoggable(definition));
    return LogMetricDeferredInternal<Result>(
        [callback = std::move(
             callback)]() mutable -> absl::flat_hash_map<std::string, Result> {
          return {{"", std::move(callback)()}};
        },
        definition);
  }

  template <const auto& definition, typename T>
  absl::Status LogMetricDeferred(
      T&& callback,
      std::enable_if_t<!std::is_arithmetic_v<std::invoke_result_t<T>>>* =
          nullptr,
      std::enable_if_t<!std::is_lvalue_reference_v<T>>* = nullptr) {
    using Result = std::invoke_result_t<T>;
    static_assert(
        std::is_same_v<absl::flat_hash_map<typename Result::key_type,
                                           typename Result::mapped_type>,
                       std::remove_cv_t<Result>>);
    CheckDefinition<definition, typename Result::mapped_type>();
    PS_RETURN_IF_ERROR(AssertLoggable(definition));
    static_assert(definition.type_instrument ==
                  Instrument::kPartitionedCounter);
    return LogMetricDeferredInternal<typename Result::mapped_type>(
        std::move(callback), definition);
  }

  template <typename T, Privacy privacy, Instrument instrument>
  absl::Status LogMetricDeferredInternal(
      absl::AnyInvocable<const absl::flat_hash_map<std::string, T>() &&>
          callback,
      const Definition<T, privacy, instrument>& definition)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    PS_ASSIGN_OR_RETURN(const bool log_safe, ShouldLogSafe<privacy>());
    absl::MutexLock mutex_lock(&mutex_);
    callbacks_.push_back([callback = std::move(callback), &definition, log_safe,
                          this]() mutable -> absl::Status {
      absl::flat_hash_map<std::string, T> values = std::move(callback)();
      if (log_safe) {
        for (auto& [partition, value] : values) {
          PS_RETURN_IF_ERROR(LogSafe(definition, value, partition));
        }
        if (metric_router_->metric_config().MetricMode() !=
            TelemetryConfig::COMPARE) {
          return absl::OkStatus();
        }
      }
      if constexpr (privacy == Privacy::kImpacting) {
        for (auto& [partition, value] :
             BoundPartitionsContributed(values, definition)) {
          PS_RETURN_IF_ERROR(
              metric_router_->LogUnSafe(definition, value, partition));
        }
      }
      return absl::OkStatus();
    });
    return absl::OkStatus();
  }

  template <typename ValueT, typename MetricT>
  std::vector<std::pair<std::string, ValueT>> BoundPartitionsContributed(
      const absl::flat_hash_map<std::string, ValueT>& value,
      const MetricT& definition) {
    return BoundPartitionsContributed(value, definition, definition.name_);
  }

  // For Privacy kImpacting partitioned metrics, partitions must be in defined
  // `public_partitions_`, the number of partitions contributed by each privacy
  // unit is limited to `max_partitions_contributed_`; Returns the metric values
  // with upto limited number of partitions.
  template <typename T>
  std::vector<std::pair<std::string, T>> BoundPartitionsContributed(
      const absl::flat_hash_map<std::string, T>& value,
      const internal::Partitioned& partitioned, absl::string_view name) {
    std::vector<std::pair<std::string, T>> ret;
    if (absl::Span<const absl::string_view> public_partitions =
            metric_router_->metric_config().GetPartition(partitioned, name);
        !public_partitions.empty()) {
      for (auto& [partition, numeric] : value) {
        if (absl::c_binary_search(public_partitions, partition)) {
          ret.emplace_back(partition, numeric);
        } else {
          ABSL_LOG_EVERY_N_SEC(WARNING, 60)
              << partition << " is not in public_partitions_ ["
              << partitioned.partition_type_ << "] of metric:" << name;
        }
        if (ret.size() >= partitioned.max_partitions_contributed_) {
          break;
        }
      }
    } else {
      // In this case, `public_partitions_` is not defined. if
      // `max_partitions_contributed_` = 1, then it is not partitioned metric,
      // just return the value; otherwise it is private parition metric that
      // is not implemented yet, log a warning.
      ABSL_LOG_IF_EVERY_N_SEC(WARNING,
                              partitioned.max_partitions_contributed_ > 1, 600)
          << "public_partitions_ not defined for metric : " << name;
      ret.insert(ret.begin(), value.begin(), value.end());
      if (ret.size() >= partitioned.max_partitions_contributed_) {
        ret.resize(partitioned.max_partitions_contributed_);
      }
    }
    return ret;
  }

  U* metric_router_;
  bool decrypted_ = false;
  bool is_request_successful_ = false;
  absl::Mutex mutex_;
  std::vector<absl::AnyInvocable<absl::Status() &&>> callbacks_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<const DefinitionName*> logged_metric_
      ABSL_GUARDED_BY(mutex_);

  struct Accumulator {
    using PartitionedValue = absl::flat_hash_map<std::string, double>;
    PartitionedValue values;
    absl::AnyInvocable<absl::Status(const PartitionedValue&) &&> callback;
  };
  absl::flat_hash_map<const DefinitionName*, Accumulator> accumulated_metric_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace privacy_sandbox::server_common::metric

#endif  // SERVICES_COMMON_METRIC_CONTEXT_H_
