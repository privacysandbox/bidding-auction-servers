/*
 * Copyright 2024 Google LLC
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

#include "services/bidding_service/inference/periodic_model_fetcher.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>

#include <google/protobuf/util/json_util.h>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/inference_sidecar.pb.h"
#include "proto/model_metadata.pb.h"
#include "services/bidding_service/inference/model_fetcher_metric.h"
#include "services/common/blob_fetch/blob_fetcher_base.h"
#include "src/logger/request_context_impl.h"
#include "src/util/status_macro/status_macros.h"
#include "src/util/status_macro/status_util.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Currently max proto message is at 2 Gib per message.
constexpr size_t kMaxProtoMessageSize = 2ULL * 1024ULL * 1024ULL * 1024ULL;
// Minimal duration to wait before trying to fetch model blobs again.
constexpr absl::Duration kMinModelFetchPeriod = absl::Minutes(1);

PeriodicModelFetcher::PeriodicModelFetcher(
    absl::string_view config_path,
    std::unique_ptr<privacy_sandbox::bidding_auction_servers::BlobFetcherBase>&&
        blob_fetcher,
    std::unique_ptr<InferenceService::StubInterface>&& inference_stub,
    server_common::Executor* executor, const absl::Duration& fetch_period_ms)
    : config_path_(config_path),
      blob_fetcher_(std::move(blob_fetcher)),
      inference_stub_(std::move(inference_stub)),
      executor_(*executor),
      fetch_period_ms_(fetch_period_ms) {}

absl::Status PeriodicModelFetcher::Start() {
  CHECK_GT(fetch_period_ms_, kMinModelFetchPeriod)
      << "Too small fetch period is prohibited, please modify "
         "INFERENCE_MODEL_FETCH_PERIOD_MS";
  InternalPeriodicModelFetchAndRegistration();
  return absl::OkStatus();
}

void PeriodicModelFetcher::DeleteModel(absl::string_view model_path)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(model_entry_mutex_) {
  model_entry_map_[model_path].model_state = ModelState::IN_DELETION;
  // TODO(b/380119479): Configure model management GRPC deadline.
  grpc::ClientContext context;
  DeleteModelRequest request;
  request.mutable_model_spec()->set_model_path(model_path);
  DeleteModelResponse response;
  grpc::Status status =
      inference_stub_->DeleteModel(&context, request, &response);
  if (!status.ok()) {
    PS_LOG(ERROR) << "Failed to delete model: " << model_path
                  << " due to: " << status.error_message();
    ModelFetcherMetric::IncrementModelDeletionFailedCountByStatus(
        server_common::ToAbslStatus(status).code());
  } else {
    model_entry_map_.erase(model_path);
    PS_LOG(INFO) << "Successful deletion of model: " << model_path;
    ModelFetcherMetric::IncrementModelDeletionSuccessCount();
  }
  UpdateMetricsForAvailableModels();
}

void PeriodicModelFetcher::DeleteModels(
    const absl::flat_hash_set<std::string>& model_paths)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(model_entry_mutex_) {
  for (const auto& model_path : model_paths) {
    ModelEntry& model_entry = model_entry_map_[model_path];
    // Case #1: If the model is IN_GRACE_PERIOD, skip it as the model deletion
    // is already scheduled.
    if (model_entry.model_state == ModelState::IN_GRACE_PERIOD) {
      continue;
    }

    // Case #2: If the model is ACTIVE and requested to be deleted, schedule the
    // model deletion after the grace period.
    if (model_entry.model_state == ModelState::ACTIVE &&
        model_entry.eviction_grace_period_in_ms > 0) {
      model_entry.model_state = ModelState::IN_GRACE_PERIOD;
      PS_LOG(INFO) << "Model eviction grace period starts for model: "
                   << model_path;
      executor_.RunAfter(
          absl::Milliseconds(model_entry.eviction_grace_period_in_ms),
          [this, model_path] {
            absl::MutexLock lock(&model_entry_mutex_);
            PS_LOG(INFO) << "Model eviction grace period ends for model: "
                         << model_path;
            DeleteModel(model_path);
          });
      continue;
    }

    // Case #3-a: If the model is ACTIVE with no grace period, delete the model
    // immediately.
    // Case #3-b: If the model failed to be deleted after the grace period with
    // the IN_DELETION status, delete the model immediately.
    DeleteModel(model_path);
  }
}

absl::StatusOr<ModelConfig> PeriodicModelFetcher::FetchModelConfig() {
  PS_RETURN_IF_ERROR(
      blob_fetcher_->FetchSync({.included_prefixes = {config_path_}}))
      << "Error loading model configuration: " << config_path_;

  // If the model config path is a valid file path, only a single file
  // should be fetched.
  const std::vector<BlobFetcherBase::Blob>& config_fetch_result =
      blob_fetcher_->snapshot();
  if (config_fetch_result.size() != 1) {
    return absl::NotFoundError("Invalid model config path.");
  }

  ModelConfig config;
  PS_RETURN_IF_ERROR(google::protobuf::util::JsonStringToMessage(
      config_fetch_result[0].bytes, &config))
      << "Could not parse model configuration from JSON.";

  return config;
}

void PeriodicModelFetcher::InternalModelFetchAndRegistration() {
  absl::MutexLock lock(&model_entry_mutex_);
  absl::StatusOr<ModelConfig> config = FetchModelConfig();
  if (!config.ok()) {
    PS_LOG(ERROR) << "Failed to fetch model config: " << config.status();
    ModelFetcherMetric::IncrementCloudFetchFailedCountByStatus(
        config.status().code());
    return;
  }

  ModelFetcherMetric::IncrementCloudFetchSuccessCount();

  // Model deletion happens upon model configuration file changes.
  // Case #1: If a model metadata (model path and checksum) stays unchanged in
  // the configuration, no deletion will happen as the model will be erased from
  // this deletion set.
  // Case #2: If a model is removed from the model configuration file, model
  // deletion will be triggered.
  // Case #3: If a model's checksum changes, model deletion will be triggered
  // for the old model content, and a model registration will happen for the new
  // model content.
  absl::flat_hash_set<std::string> garbage_collectable_models;
  for (const auto& [model_path, model_entry] : model_entry_map_) {
    garbage_collectable_models.insert(model_path);
  }

  // Keeps track of models that are registered successfully and those that are
  // not for metric purposes.
  std::vector<std::string> success_models;
  std::vector<std::string> failure_models;
  absl::flat_hash_map<std::string, double> pre_warm_latency_metric_map;
  BlobFetcherBase::FilterOptions filter_options;
  std::vector<ModelMetadata> pending_model_metadata;
  // Processes model deletion.
  for (const ModelMetadata& metadata : config->model_metadata()) {
    const std::string& model_path = metadata.model_path();
    auto it = model_entry_map_.find(model_path);
    if (it != model_entry_map_.end() &&
        it->second.checksum == metadata.checksum()) {
      // The model with the matching checksum is already loaded.
      // It should not be deleted.
      garbage_collectable_models.erase(model_path);
    }
  }
  DeleteModels(garbage_collectable_models);

  // Processes model loading.
  for (const ModelMetadata& metadata : config->model_metadata()) {
    const std::string& model_path = metadata.model_path();
    if (model_path.empty()) {
      continue;
    }

    if (auto it = model_entry_map_.find(model_path);
        it != model_entry_map_.end()) {
      // When a model is overwritten with a different checksum, a deletion
      // request is scheduled in the previous `DeleteModels` call. If deletion
      // succeeds, the model is removed from the map and prepared for fetching.
      // If a grace period is active, model fetching is postponed until the next
      // polling cycle after the grace period has expired.
      if (it->second.model_state == ModelState::IN_GRACE_PERIOD) {
        PS_LOG(ERROR) << "Model registration failed for: " << model_path
                      << " - model is in eviction grace period.";
        failure_models.push_back(model_path);
        ModelFetcherMetric::IncrementModelRegistrationFailedCountByStatus(
            absl::StatusCode::kUnavailable);
      } else if (it->second.model_state == ModelState::IN_DELETION) {
        PS_LOG(ERROR) << "Model registration failed for: " << model_path
                      << " - previous deletion attempt failed.";
        failure_models.push_back(model_path);
        ModelFetcherMetric::IncrementModelRegistrationFailedCountByStatus(
            absl::StatusCode::kFailedPrecondition);
      }
      continue;
    }

    filter_options.included_prefixes.push_back(model_path);
    pending_model_metadata.push_back(metadata);
  }

  if (filter_options.included_prefixes.empty()) {
    PS_LOG(INFO) << "No additional model to load.";
    return;
  }

  absl::Status cloud_fetch_status = blob_fetcher_->FetchSync(filter_options);
  if (!cloud_fetch_status.ok()) {
    PS_LOG(ERROR) << "Cloud model fetching fails: " << cloud_fetch_status;
    ModelFetcherMetric::IncrementCloudFetchFailedCountByStatus(
        cloud_fetch_status.code());
    return;
  }

  const std::vector<BlobFetcherBase::Blob>& bucket_snapshot =
      blob_fetcher_->snapshot();
  // A single model can consist of multiple model files and hence data blobs.
  for (const ModelMetadata& metadata : pending_model_metadata) {
    const std::string& model_path = metadata.model_path();
    RegisterModelRequest request;
    request.mutable_model_spec()->set_model_path(model_path);
    if (!metadata.warm_up_batch_request_json().empty()) {
      request.set_warm_up_batch_request_json(
          metadata.warm_up_batch_request_json());
    }
    PS_VLOG(10) << "Start registering model for: " << model_path;

    std::vector<BlobFetcherBase::BlobView> blob_views;
    for (const BlobFetcherBase::Blob& blob : bucket_snapshot) {
      // When model path ends with "/", we match all files under the directory.
      // When model path does not end with "/", we perform exact matching.
      if ((absl::EndsWith(model_path, "/") &&
           absl::StartsWith(blob.path, model_path)) ||
          blob.path == model_path) {
        (*request.mutable_model_files())[blob.path] = blob.bytes;
        blob_views.push_back(blob.CreateBlobView());
      }
    }
    // Check if file size is over allowed proto message limit size.
    if (request.ByteSizeLong() > kMaxProtoMessageSize) {
      PS_LOG(ERROR) << "Skip registering model for: " << model_path
                    << " Detect oversized model have size:"
                    << request.ByteSizeLong()
                    << " and allowed max proto size:" << kMaxProtoMessageSize;
      ModelFetcherMetric::IncrementModelRegistrationFailedCountByStatus(
          absl::StatusCode::kFailedPrecondition);
      failure_models.push_back(model_path);
      continue;
    }

    // Model checksum is currently an optional field for loading a model.
    if (!metadata.checksum().empty()) {
      absl::StatusOr<std::string> model_checksum =
          ComputeChecksumForBlobs(blob_views);
      if (!model_checksum.ok() || *model_checksum != metadata.checksum()) {
        PS_LOG(ERROR) << "Model rejected due to incorrect checksum."
                      << " model_path=" << model_path
                      << " status=" << model_checksum.status();
        if (model_checksum.ok()) {
          PS_LOG(ERROR) << "actual_checksum=" << *model_checksum;
        }
        failure_models.push_back(model_path);
        ModelFetcherMetric::IncrementModelRegistrationFailedCountByStatus(
            absl::StatusCode::kFailedPrecondition);
        continue;
      }
    }

    grpc::ClientContext context;
    RegisterModelResponse response;
    grpc::Status status =
        inference_stub_->RegisterModel(&context, request, &response);

    if (!status.ok()) {
      PS_LOG(ERROR) << "Registering model failure for: " << model_path
                    << " because of " << status.error_message();
      failure_models.push_back(model_path);
      ModelFetcherMetric::IncrementModelRegistrationFailedCountByStatus(
          server_common::ToAbslStatus(status).code());
    } else {
      PS_VLOG(10) << "Registering model success for: " << model_path;
      model_entry_map_[model_path] = {
          .checksum = metadata.checksum(),
          .eviction_grace_period_in_ms = metadata.eviction_grace_period_in_ms(),
          .model_state = ModelState::ACTIVE};
      success_models.push_back(model_path);
      if (response.metrics_list_size() != 0) {
        if (response.metrics_list().find(
                "kInferenceRegisterModelResponseModelWarmUpDuration") !=
            response.metrics_list().end()) {
          pre_warm_latency_metric_map.insert(
              {model_path,
               response.metrics_list()
                   .at("kInferenceRegisterModelResponseModelWarmUpDuration")
                   .metrics()
                   .at(0)
                   .value_double()});
        }
      }
    }
  }
  ModelFetcherMetric::UpdateRecentModelRegistrationSuccess(success_models);
  ModelFetcherMetric::UpdateRecentModelRegistrationFailure(failure_models);
  ModelFetcherMetric::UpdateModelRegistrationPrewarmLatency(
      pre_warm_latency_metric_map);
  UpdateMetricsForAvailableModels();
}

void PeriodicModelFetcher::UpdateMetricsForAvailableModels(void)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(model_entry_mutex_) {
  std::vector<std::string> current_models;
  for (const auto& [model_path, model_entry] : model_entry_map_) {
    current_models.push_back(model_path);
  }
  SetModelPartition(current_models);
  ModelFetcherMetric::UpdateAvailableModels(current_models);
}

void PeriodicModelFetcher::InternalPeriodicModelFetchAndRegistration() {
  InternalModelFetchAndRegistration();
  task_id_ = executor_.RunAfter(fetch_period_ms_, [this]() {
    InternalPeriodicModelFetchAndRegistration();
  });
}

void PeriodicModelFetcher::End() {
  if (task_id_) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
