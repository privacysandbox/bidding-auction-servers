/*
 * Copyright 2024 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_PYTORCH_H_
#define SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_PYTORCH_H_

#include <memory>
#include <utility>

#include <torch/torch.h>

#include "absl/status/statusor.h"
#include "model/model_store.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

class PyTorchModule final : public ModuleInterface {
 public:
  explicit PyTorchModule(const InferenceSidecarRuntimeConfig& config);

  absl::StatusOr<PredictResponse> Predict(
      const PredictRequest& request,
      const RequestContext& request_context) override;
  absl::StatusOr<RegisterModelResponse> RegisterModel(
      const RegisterModelRequest& request) override;

 private:
  friend class PyTorchModuleResetModelTest;
  void SetModelStoreForTestOnly(
      std::unique_ptr<ModelStore<torch::jit::script::Module>> store) {
    store_ = std::move(store);
  }

  const InferenceSidecarRuntimeConfig runtime_config_;

  // Stores a set of models. It's thread safe.
  std::unique_ptr<ModelStore<torch::jit::script::Module>> store_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_MODULES_PYTORCH_V2_1_1_PYTORCH_H_
