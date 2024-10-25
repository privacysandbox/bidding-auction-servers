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

#include "modules/test_module.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>

#include "absl/log/absl_log.h"
#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "modules/module_interface.h"
#include "proto/inference_sidecar.pb.h"
#include "sandboxed_api/util/runfiles.h"

#include "test_constants.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

TestModule::~TestModule() {
  if (model_ptr_ != nullptr) {
    CHECK(munmap(model_ptr_, model_size_) == 0) << "Failed to munmap.";
  }
  if (model_fd_ != -1) {
    close(model_fd_);
  }
}

absl::StatusOr<PredictResponse> TestModule::Predict(
    const PredictRequest& request, const RequestContext& request_context) {
  // For testing consented debugging.
  INFERENCE_LOG(INFO, request_context) << kConsentedLogMsg;
  // Returns a placeholder value.
  PredictResponse response;
  response.set_output("0.57721");
  return response;
}

absl::StatusOr<RegisterModelResponse> TestModule::RegisterModel(
    const RegisterModelRequest& request) {
  // Returns empty response.
  RegisterModelResponse response;

  if (!model_path_.empty()) {
    std::string path = sapi::GetDataDependencyFilePath(model_path_);
    model_fd_ = open(path.c_str(), O_RDONLY);
    if (model_fd_ == -1) {
      ABSL_LOG(ERROR) << "Failed to read the model: " << path;
      return response;
    }
    model_size_ = lseek(model_fd_, 0, SEEK_END);
    if (model_size_ <= 0) {
      ABSL_LOG(ERROR) << "Empty file. Size=" << model_size_;
      return response;
    }
    model_ptr_ = mmap(0, model_size_, PROT_READ, MAP_PRIVATE, model_fd_, 0);
    if (model_ptr_ == MAP_FAILED) {
      ABSL_LOG(ERROR) << "Failed to mmap.";
      return response;
    }
  }
  return response;
}

absl::StatusOr<DeleteModelResponse> TestModule::DeleteModel(
    const DeleteModelRequest& request) {
  const DeleteModelResponse response;
  if (munmap(model_ptr_, model_size_) == -1) {
    ABSL_LOG(ERROR) << "Failed to munmap.";
    return response;
  }

  if (close(model_fd_) == -1) {
    ABSL_LOG(ERROR) << "Failed to close the file.";
    return response;
  }

  model_fd_ = -1;
  model_size_ = 0;
  model_ptr_ = nullptr;
  model_path_ = "";

  return response;
}

std::unique_ptr<ModuleInterface> ModuleInterface::Create(
    const InferenceSidecarRuntimeConfig& config) {
  return std::make_unique<TestModule>(config);
}

absl::string_view ModuleInterface::GetModuleVersion() { return "test"; }

}  // namespace privacy_sandbox::bidding_auction_servers::inference
