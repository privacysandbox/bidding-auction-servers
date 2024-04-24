// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "utils/file_util.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "absl/base/log_severity.h"
#include "absl/log/absl_log.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

namespace privacy_sandbox::bidding_auction_servers::inference {
namespace {

absl::StatusOr<std::string> ReadFile(absl::string_view path) {
  static absl::Mutex mutex;
  absl::MutexLock lock(&mutex);

  std::ifstream ifs(path.data(), std::ios::in | std::ios::binary);
  if (!ifs.good()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to load file from path: ", path));
  }
  return std::string(std::istreambuf_iterator<char>(ifs),
                     std::istreambuf_iterator<char>());
}

}  // namespace

absl::Status PopulateRegisterModelRequest(absl::string_view path,
                                          RegisterModelRequest& request,
                                          bool log_on_error) {
  request.mutable_model_spec()->set_model_path(std::string(path));

  if (std::filesystem::is_directory(path)) {
    // Tensorflow models consist of multiple files under a directroy.
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(path)) {
      if (entry.is_regular_file()) {
        const std::string& entry_path = entry.path().string();
        absl::StatusOr<std::string> bytes = ReadFile(entry_path);
        if (!bytes.ok()) {
          ABSL_LOG_IF(ERROR, log_on_error) << bytes.status();
          return bytes.status();
        }
        (*request.mutable_model_files())[entry_path] = std::move(*bytes);
      }
    }
    return absl::OkStatus();
  }

  // PyTorch model is exactly one file. We read it here and put the byte string
  // in request. PyTorch sidecar loads the model with RegisterModelRequest.
  absl::StatusOr<std::string> bytes = ReadFile(path);
  if (!bytes.ok()) {
    ABSL_LOG_IF(ERROR, log_on_error) << bytes.status();
    return bytes.status();
  }
  (*request.mutable_model_files())[path] = std::move(*bytes);
  return absl::OkStatus();
}

}  // namespace privacy_sandbox::bidding_auction_servers::inference
