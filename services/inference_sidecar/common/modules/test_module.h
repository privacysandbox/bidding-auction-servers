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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_MODULES_TEST_MODULE_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_MODULES_TEST_MODULE_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// TestModule is the module used for testing. The module is buildable in the
// main B&A workspace while TensorFlow and PyTorch modules should be built in
// separate workspaces. The module implements very simple logic for each API;
// oftentimes, the implementation is a no-op.
class TestModule final : public ModuleInterface {
 public:
  // The constructor maps the embedded model file into memory.
  explicit TestModule(const InferenceSidecarRuntimeConfig& config)
      : config_(config) {}
  ~TestModule() override;

  absl::StatusOr<PredictResponse> Predict(
      const PredictRequest& request,
      const RequestContext& request_context) override;
  absl::StatusOr<RegisterModelResponse> RegisterModel(
      const RegisterModelRequest& request) override;
  absl::StatusOr<DeleteModelResponse> DeleteModel(
      const DeleteModelRequest& request) override;

  void set_model_path(absl::string_view path) { model_path_ = path; }

  int model_size() const { return model_size_; }

 private:
  // The file descriptor of the memory mapped model file.
  int model_fd_ = -1;
  // The size of the model file.
  int model_size_ = 0;
  // The memory address of the memory mapped model file.
  void* model_ptr_ = nullptr;
  // The model path.
  std::string model_path_ = "";
  const InferenceSidecarRuntimeConfig config_;
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_MODULES_TEST_MODULE_H_
