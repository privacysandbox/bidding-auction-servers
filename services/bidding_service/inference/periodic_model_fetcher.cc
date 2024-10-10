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

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
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
  InternalPeriodicModelFetchAndRegistration();
  return absl::OkStatus();
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
  absl::StatusOr<ModelConfig> config = FetchModelConfig();
  if (!config.ok()) {
    PS_LOG(ERROR) << "Failed to fetch model config: " << config.status();
    ModelFetcherMetric::IncrementCloudFetchFailedCountByStatus(
        config.status().code());
    return;
  }

  BlobFetcherBase::FilterOptions filter_options;
  std::vector<ModelMetadata> pending_model_metadata;
  for (const ModelMetadata& metadata : config->model_metadata()) {
    const std::string& model_path = metadata.model_path();
    if (!model_path.empty() &&
        current_models_.find(model_path) == current_models_.end()) {
      filter_options.included_prefixes.push_back(model_path);
      pending_model_metadata.push_back(metadata);
    }
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

  ModelFetcherMetric::IncrementCloudFetchSuccessCount();

  // Keeps track of models that are registered successfully and those that are
  // not for metric purposes.
  std::vector<std::string> success_models;
  std::vector<std::string> failure_models;
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
      if (absl::StartsWith(blob.path, model_path)) {
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
        PS_LOG(ERROR) << "Model rejected due to incorrect checksum.";
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
      PS_LOG(ERROR) << "Registering model failure for: " << model_path;
      failure_models.push_back(model_path);
      ModelFetcherMetric::IncrementModelRegistrationFailedCountByStatus(
          server_common::ToAbslStatus(status).code());
    } else {
      PS_VLOG(10) << "Registering model success for: " << model_path;
      current_models_.insert(model_path);
      success_models.push_back(model_path);
    }
  }

  ModelFetcherMetric::UpdateRecentModelRegistrationSuccess(success_models);
  // We only reset the metrics partitioned by model when new models are loaded.
  if (!success_models.empty()) {
    SetModelPartition(std::vector<std::string>{current_models_.begin(),
                                               current_models_.end()});
  }

  ModelFetcherMetric::UpdateRecentModelRegistrationFailure(failure_models);
  ModelFetcherMetric::UpdateAvailableModels(
      {current_models_.begin(), current_models_.end()});
}

void PeriodicModelFetcher::InternalPeriodicModelFetchAndRegistration() {
  InternalModelFetchAndRegistration();
  task_id_ = executor_.RunAfter(fetch_period_ms_, [this]() {
    InternalPeriodicModelFetchAndRegistration();
  });
}

void PeriodicModelFetcher::End() {
  if (task_id_.has_value()) {
    executor_.Cancel(*task_id_);
    task_id_ = absl::nullopt;
  }
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
