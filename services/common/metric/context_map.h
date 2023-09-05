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

#ifndef SERVICES_COMMON_METRIC_CONTEXT_MAP_H_
#define SERVICES_COMMON_METRIC_CONTEXT_MAP_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "services/common/metric/context.h"
#include "services/common/metric/metric_router.h"
#include "services/common/telemetry/telemetry_flag.h"

namespace privacy_sandbox::server_common::metric {

// ContextMap provide a thread-safe map between T* and `Context`.
// T should be a request received by server, i.e. `GenerateBidsRequest`.
// See detail docs about `L` and `U` at `Context`.
template <typename T, const absl::Span<const DefinitionName* const>& L,
          typename U>
class ContextMap {
 public:
  using ContextT = Context<L, U>;

  ContextMap(std::unique_ptr<U> metric_router, BuildDependentConfig config)
      : metric_router_(std::move(metric_router)),
        metric_config_(std::move(config)) {
    CHECK_OK(CheckListOrder());
  }
  ~ContextMap() = default;

  // ContextMap is neither copyable nor movable.
  ContextMap(const ContextMap&) = delete;
  ContextMap& operator=(const ContextMap&) = delete;

  // Get the `Context` tied to a T*, create new if not exist, `ContextMap` owns
  // the `Context`.
  ContextT& Get(T* t) ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    auto it = context_.find(t);
    if (it == context_.end()) {
      it = context_
               .emplace(t, ContextT::GetContext(metric_router_.get(),
                                                metric_config_))
               .first;
    }
    return *it->second;
  }

  // Release the ownership of the `Context` tied to a T* and return it.
  absl::StatusOr<std::unique_ptr<ContextT>> Remove(T* t)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock mutex_lock(&mutex_);
    auto it = context_.find(t);
    if (it == context_.end()) {
      return absl::NotFoundError("Metric context not found");
    }
    std::unique_ptr<ContextT> c = std::move(it->second);
    context_.erase(it);
    return c;
  }

  template <typename TDefinition>
  absl::Status AddObserverable(
      const TDefinition& definition,
      absl::flat_hash_map<std::string, double> (*callback)()) {
    return metric_router_->AddObserverable(definition, callback);
  }

  const BuildDependentConfig& metric_config() const { return metric_config_; }
  const U* metric_router() const { return metric_router_.get(); }

  absl::Status CheckListOrder() {
    for (auto* definition : L) {
      if (!std::is_sorted(definition->histogram_boundaries_copy_.begin(),
                          definition->histogram_boundaries_copy_.end())) {
        return absl::InvalidArgumentError(absl::StrCat(
            definition->name_, " histogram boundaries is out of order"));
      }
      if (!std::is_sorted(definition->public_partitions_copy_.begin(),
                          definition->public_partitions_copy_.end())) {
        return absl::InvalidArgumentError(absl::StrCat(
            definition->name_, " public partitions is out of order"));
      }
    }
    return absl::OkStatus();
  }

 private:
  std::unique_ptr<U> metric_router_;
  const BuildDependentConfig metric_config_;
  absl::Mutex mutex_;
  absl::flat_hash_map<T*, std::unique_ptr<ContextT>> context_
      ABSL_GUARDED_BY(mutex_);
};

// Get singleton `ContextMap` for T. First call will initialize.
// `config` must have a value at first call, in following calls can be omitted
// or has a same value. `provider` being null will be initialized with default
// MeterProvider without metric exporting.
template <typename T, const absl::Span<const DefinitionName* const>& L>
inline auto* GetContextMap(
    std::optional<BuildDependentConfig> config,
    std::unique_ptr<MetricRouter::MeterProvider> provider,
    absl::string_view service, absl::string_view version,
    PrivacyBudget budget) {
  static auto* context_map = [&]() mutable {
    CHECK(config != std::nullopt) << "cannot be null at initialization";
    absl::Status config_status = config->CheckMetricConfig(L);
    ABSL_LOG_IF(WARNING, !config_status.ok()) << config_status;
    double total_weight = absl::c_accumulate(
        L, 0.0, [&config](double total, const DefinitionName* definition) {
          return total += config->GetMetricConfig(definition->name_).ok()
                              ? definition->privacy_budget_weight_copy_
                              : 0;
        });
    budget.epsilon /= total_weight;
    return new ContextMap<T, L, MetricRouter>(
        std::make_unique<MetricRouter>(
            std::move(provider), service, version, budget,
            absl::Milliseconds(config->dp_export_interval_ms())),
        *config);
  }();
  CHECK(config == std::nullopt ||
        config->IsDebug() == context_map->metric_config().IsDebug())
      << "Must be null or same value after initialized";
  return context_map;
}

template <const absl::Span<const DefinitionName* const>& L>
using ServerContext = Context<L, MetricRouter>;

}  // namespace privacy_sandbox::server_common::metric

#endif  // SERVICES_COMMON_METRIC_CONTEXT_MAP_H_
