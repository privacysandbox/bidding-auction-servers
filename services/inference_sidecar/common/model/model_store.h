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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_MODEL_MODEL_STORE_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_MODEL_MODEL_STORE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "proto/inference_sidecar.pb.h"
#include "src/util/status_macro/status_macros.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

class ModelStoreTest;

// Class that manages models for the inference sidecar. It keeps shared
// ownership of the models. The accepted models can either be ones that accept
// consented traffic and ones that accept production traffic. The interface is
// thread-safe, assuming the  ModelType also exposes a thread-safe interface.
template <typename ModelType>
class ModelStore {
 public:
  using ModelConstructor =
      absl::AnyInvocable<absl::StatusOr<std::unique_ptr<ModelType>>(
          const InferenceSidecarRuntimeConfig&, const RegisterModelRequest&)
                             const>;

  explicit ModelStore(const InferenceSidecarRuntimeConfig& config,
                      ModelConstructor model_constructor)
      : config_(config), model_constructor_(std::move(model_constructor)) {}

  // Puts model into the model store. Creates both a copy for consented traffic
  // and a copy for prod traffic. Future reset calls use the saved RegisterModel
  // Request. Overwrites a model entry if the key already exists.
  // This method is thread-safe.
  absl::Status PutModel(absl::string_view key,
                        const RegisterModelRequest& request) {
    PS_ASSIGN_OR_RETURN(std::unique_ptr<ModelType> prod_model,
                        model_constructor_(config_, request));
    PS_ASSIGN_OR_RETURN(std::unique_ptr<ModelType> consented_model,
                        model_constructor_(config_, request));

    absl::MutexLock model_data_lock(&model_data_mutex_);
    model_data_map_[key] = request;

    absl::MutexLock prod_model_lock(&prod_model_mutex_);
    prod_model_map_[key] = std::move(prod_model);

    absl::MutexLock consented_model_lock(&consented_model_mutex_);
    consented_model_map_[key] = std::move(consented_model);

    return absl::OkStatus();
  }

  // Gets a model for serving. Returns an error status if a given key is not
  // found.
  // This method is thread-safe.
  absl::StatusOr<std::shared_ptr<ModelType>> GetModel(
      absl::string_view key, bool is_consented = false) const {
    const absl::flat_hash_map<std::string, std::shared_ptr<ModelType>>&
        model_map = is_consented ? consented_model_map_ : prod_model_map_;
    absl::MutexLock lock(is_consented ? &consented_model_mutex_
                                      : &prod_model_mutex_);

    auto it = model_map.find(key);
    if (it == model_map.end()) {
      return absl::NotFoundError(
          absl::StrCat("Requested model '", key, "' has not been registered"));
    }
    return it->second;
  }

  // Reset a model entry using the model constructor.
  // This method is thread-safe.
  absl::Status ResetModel(absl::string_view key, bool is_consented = false) {
    absl::MutexLock model_data_lock(&model_data_mutex_);
    auto it = model_data_map_.find(key);
    if (it == model_data_map_.end()) {
      return absl::NotFoundError(
          absl::StrCat("Resetting model '", key,
                       "' fails because it has not been registered"));
    }
    const RegisterModelRequest& request = it->second;
    PS_ASSIGN_OR_RETURN(std::unique_ptr<ModelType> model,
                        model_constructor_(config_, request));

    absl::flat_hash_map<std::string, std::shared_ptr<ModelType>>& model_map =
        is_consented ? consented_model_map_ : prod_model_map_;
    absl::MutexLock model_lock(is_consented ? &consented_model_mutex_
                                            : &prod_model_mutex_);
    model_map[key] = std::move(model);
    return absl::OkStatus();
  }

  std::vector<std::string> ListModels() const {
    std::vector<std::string> model_keys;
    absl::MutexLock model_data_lock(&model_data_mutex_);
    for (auto& [key, value] : model_data_map_) {
      model_keys.push_back(key);
    }
    return model_keys;
  }

  ModelStore(const ModelStore&) = delete;
  ModelStore& operator=(const ModelStore&) = delete;

  ModelStore(ModelStore&&) = delete;
  ModelStore& operator=(ModelStore&&) = delete;

 protected:
  // Used for test only, the caller needs to ensure thread-safety.
  void SetModelConstructorForTestOnly(ModelConstructor model_constructor) {
    model_constructor_ = std::move(model_constructor);
  }

 private:
  // Always lock on `model_data_mutex_` before `prod_model_mutex_` and
  // then `consented_model_mutex_` to avoid deadlock.
  mutable absl::Mutex model_data_mutex_ ABSL_ACQUIRED_BEFORE(prod_model_mutex_);
  mutable absl::Mutex prod_model_mutex_
      ABSL_ACQUIRED_BEFORE(consented_model_mutex_);
  mutable absl::Mutex consented_model_mutex_;
  absl::flat_hash_map<std::string, RegisterModelRequest> model_data_map_
      ABSL_GUARDED_BY(model_data_mutex_);
  absl::flat_hash_map<std::string, std::shared_ptr<ModelType>> prod_model_map_
      ABSL_GUARDED_BY(prod_model_mutex_);
  absl::flat_hash_map<std::string, std::shared_ptr<ModelType>>
      consented_model_map_ ABSL_GUARDED_BY(consented_model_mutex_);

  const InferenceSidecarRuntimeConfig config_;
  ModelConstructor model_constructor_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_MODEL_MODEL_STORE_H_
