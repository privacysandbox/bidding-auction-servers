//  Copyright 2024 Google LLC
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

#ifndef SERVICES_BIDDING_SERVICE_INFERENCE_MODEL_FETCHER_METRIC_H_
#define SERVICES_BIDDING_SERVICE_INFERENCE_MODEL_FETCHER_METRIC_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "services/common/metric/server_definition.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Class that records metrics associated with the inference model fetcher.
// It keeps track of metrics such as cloud fetch success counts, cloud fetch
// failure counts, model registration success counts, and model registration
// failure counts.
// These metrics are not in the critical request path.
class ModelFetcherMetric {
 public:
  static absl::flat_hash_map<std::string, double> GetCloudFetchSuccessCount()
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return absl::flat_hash_map<std::string, double>{
        {"cloud fetch", cloud_fetch_success_count_}};
  }

  static absl::flat_hash_map<std::string, double>
  GetCloudFetchFailedCountByStatus() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return cloud_fetch_failure_count_by_error_code_;
  }

  static absl::flat_hash_map<std::string, double>
  GetRecentModelRegistrationSuccess() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return recent_model_registration_success_;
  }

  static absl::flat_hash_map<std::string, double>
  GetRecentModelRegistrationFailure() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return recent_model_registration_failure_;
  }

  static absl::flat_hash_map<std::string, double>
  GetModelRegistrationFailedCountByStatus() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return model_registration_failure_count_by_error_code_;
  }

  static absl::flat_hash_map<std::string, double> GetAvailableModels()
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    return available_models_;
  }

  static void IncrementCloudFetchFailedCountByStatus(
      absl::StatusCode error_code) ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    ++cloud_fetch_failure_count_by_error_code_[absl::StatusCodeToString(
        error_code)];
  }

  static void IncrementCloudFetchSuccessCount() ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    ++cloud_fetch_success_count_;
  }

  static void UpdateRecentModelRegistrationSuccess(
      const std::vector<std::string>& models) ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    recent_model_registration_success_.clear();
    for (const auto& model : models) {
      ++recent_model_registration_success_[model];
    }
  }

  static void UpdateRecentModelRegistrationFailure(
      const std::vector<std::string>& models) ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    recent_model_registration_failure_.clear();
    for (const auto& model : models) {
      ++recent_model_registration_failure_[model];
    }
  }

  static void IncrementModelRegistrationFailedCountByStatus(
      absl::StatusCode error_code) ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    ++model_registration_failure_count_by_error_code_[absl::StatusCodeToString(
        error_code)];
  }

  static void UpdateAvailableModels(const std::vector<std::string>& models)
      ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    available_models_.clear();
    for (const auto& model : models) {
      ++available_models_[model];
    }
  }

 private:
  ABSL_CONST_INIT static inline absl::Mutex mu_{absl::kConstInit};

  static inline int cloud_fetch_success_count_ ABSL_GUARDED_BY(mu_){0};

  static inline absl::flat_hash_map<std::string, double>
      cloud_fetch_failure_count_by_error_code_ ABSL_GUARDED_BY(mu_){};

  static inline absl::flat_hash_map<std::string, double>
      recent_model_registration_success_ ABSL_GUARDED_BY(mu_){};

  static inline absl::flat_hash_map<std::string, double>
      recent_model_registration_failure_ ABSL_GUARDED_BY(mu_){};

  static inline absl::flat_hash_map<std::string, double>
      model_registration_failure_count_by_error_code_ ABSL_GUARDED_BY(mu_){};

  static inline absl::flat_hash_map<std::string, double> available_models_
      ABSL_GUARDED_BY(mu_){};
};

// Adds model fetcher metrics to a metric context map to bidding server.
inline absl::Status AddModelFetcherMetricToBidding() {
  auto* context_map = metric::BiddingContextMap();
  PS_RETURN_IF_ERROR(context_map->AddObserverable(
      metric::kInferenceCloudFetchSuccessCount,
      inference::ModelFetcherMetric::GetCloudFetchSuccessCount));
  PS_RETURN_IF_ERROR(context_map->AddObserverable(
      metric::kInferenceCloudFetchFailedCountByStatus,
      inference::ModelFetcherMetric::GetCloudFetchFailedCountByStatus));
  PS_RETURN_IF_ERROR(context_map->AddObserverable(
      metric::kInferenceRecentModelRegistrationSuccess,
      inference::ModelFetcherMetric::GetRecentModelRegistrationSuccess));
  PS_RETURN_IF_ERROR(context_map->AddObserverable(
      metric::kInferenceRecentModelRegistrationFailure,
      inference::ModelFetcherMetric::GetRecentModelRegistrationFailure));
  PS_RETURN_IF_ERROR(context_map->AddObserverable(
      metric::kInferenceAvailableModels,
      inference::ModelFetcherMetric::GetAvailableModels));
  return context_map->AddObserverable(
      metric::kInferenceModelRegistrationFailedCountByStatus,
      inference::ModelFetcherMetric::GetModelRegistrationFailedCountByStatus);
}

inline void SetModelPartition(const std::vector<std::string>& partitions) {
  auto* context_map = metric::BiddingContextMap();
  context_map->ResetPartitionAsync(
      {metric::kInferenceRequestCountByModel.name_,
       metric::kInferenceRequestDurationByModel.name_,
       metric::kInferenceRequestFailedCountByModel.name_},
      partitions, partitions.size());
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_BIDDING_SERVICE_INFERENCE_MODEL_FETCHER_METRIC_H_
