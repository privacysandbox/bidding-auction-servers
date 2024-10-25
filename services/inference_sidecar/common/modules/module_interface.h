/*
 * Copyright 2023 Google LLC
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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_MODULES_MODULE_INTERFACE_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_MODULES_MODULE_INTERFACE_H_

#include <memory>

#include "absl/status/statusor.h"
#include "proto/inference_sidecar.pb.h"
#include "utils/log.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

class ModuleInterface {
 public:
  // Different implementations of ModuleInterface return specialized modules,
  // for example, Tensorflow module and PyTorch module. These specialized
  // modules can only be called with the methods defined by this interface.
  static std::unique_ptr<ModuleInterface> Create(
      const InferenceSidecarRuntimeConfig& config);
  // Gets inference backend module version.
  static absl::string_view GetModuleVersion();
  virtual ~ModuleInterface() = default;
  // Executes inference on a registered ML model.
  virtual absl::StatusOr<PredictResponse> Predict(
      const PredictRequest& request,
      const RequestContext& request_context = RequestContext()) = 0;
  // Registers a new model.
  virtual absl::StatusOr<RegisterModelResponse> RegisterModel(
      const RegisterModelRequest& request) = 0;
  // Delete an existing model.
  virtual absl::StatusOr<DeleteModelResponse> DeleteModel(
      const DeleteModelRequest& request) = 0;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_MODULES_MODULE_INTERFACE_H_
