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

#ifndef SERVICES_BIDDING_SERVICE_INFERENCE_PERIODIC_MODEL_FETCHER_H_
#define SERVICES_BIDDING_SERVICE_INFERENCE_PERIODIC_MODEL_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "proto/inference_sidecar.grpc.pb.h"
#include "proto/model_metadata.pb.h"
#include "services/bidding_service/inference/inference_flags.h"
#include "services/common/blob_fetch/blob_fetcher.h"
#include "services/common/data_fetch/fetcher_interface.h"
#include "src/concurrent/executor.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Model fetcher that fetches model periodicially. After fetching the models,
// the model fetcher also registers these models with the inference sidecar. It
// requires updates to a JSON config stored in the same cloud bucket as the
// models to trigger fetching new models. It only fetches from model paths that
// it has not successfully registered with the inference sidecar.
class PeriodicModelFetcher : public FetcherInterface {
 public:
  PeriodicModelFetcher(
      absl::string_view config_path,
      std::unique_ptr<
          privacy_sandbox::bidding_auction_servers::BlobFetcherBase>&&
          blob_fetcher,
      std::unique_ptr<InferenceService::StubInterface>&& inference_stub,
      server_common::Executor* executor, const absl::Duration& fetch_period_ms);

  ~PeriodicModelFetcher() { End(); }

  PeriodicModelFetcher(const PeriodicModelFetcher&) = delete;
  PeriodicModelFetcher& operator=(const PeriodicModelFetcher&) = delete;

  // Starts to fetch models periodically.
  absl::Status Start() override;

  // Finishes model fetching.
  void End() override;

 private:
  // Fetches models and registers them with the inference sidecar periodically.
  void InternalPeriodicModelFetchAndRegistration();
  // Fetches and registers models for a single time.
  void InternalModelFetchAndRegistration();
  // Fetches the metadata of models to be downloaded from the cloud bucket.
  absl::StatusOr<ModelConfig> FetchModelConfig();

  const std::string config_path_;
  std::unique_ptr<privacy_sandbox::bidding_auction_servers::BlobFetcherBase>
      blob_fetcher_;
  std::unique_ptr<InferenceService::StubInterface> inference_stub_;
  // Async executor for periodically execute a task. Not owning.
  server_common::Executor& executor_;
  // Keeps track of the next async task for the executor.
  absl::optional<server_common::TaskId> task_id_;
  const absl::Duration fetch_period_ms_;
  // For defining the inference metric partiton and for avoid fetching the same
  // model more than once.
  absl::flat_hash_set<std::string> current_models_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_BIDDING_SERVICE_INFERENCE_PERIODIC_MODEL_FETCHER_H_
