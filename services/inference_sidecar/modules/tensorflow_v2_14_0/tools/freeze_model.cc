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

// Freezes a given TensorFlow model.
// Usage: ./builders/tools/bazel-debian run //tools:freeze_model -- --model_path
// /src/workspace/<path>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/util/status_macro/status_macros.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/cc/saved_model/loader.h"
#include "tensorflow/cc/saved_model/tag_constants.h"
#include "tensorflow/cc/tools/freeze_saved_model.h"
#include "tensorflow/core/framework/graph.pb.h"
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/protobuf/meta_graph.pb.h"
#include "tensorflow/core/public/session.h"
#include "tensorflow/core/public/version.h"

ABSL_FLAG(std::string, model_path, "", "Path to the TensorFlow model.");

namespace {

absl::Status FreezeSavedModel(tensorflow::SessionOptions& session_options,
                              tensorflow::SavedModelBundle& model_bundle) {
  tensorflow::GraphDef frozen_graph_def;
  std::unordered_set<std::string> dummy_inputs, dummy_outputs;
  PS_RETURN_IF_ERROR(tensorflow::FreezeSavedModel(
      model_bundle, &frozen_graph_def, &dummy_inputs, &dummy_outputs));

  std::unique_ptr<tensorflow::Session> frozen_session(
      tensorflow::NewSession(session_options));
  PS_RETURN_IF_ERROR(frozen_session->Create(frozen_graph_def));

  // Updates the model in place.
  model_bundle.session = std::move(frozen_session);
  *model_bundle.meta_graph_def.mutable_graph_def() =
      std::move(frozen_graph_def);

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  ABSL_LOG(INFO) << "TensorFlow Version: " << TF_VERSION_STRING << std::endl;
  ABSL_LOG(INFO) << "TensorFlow GraphDef Version: " << TF_GRAPH_DEF_VERSION
                 << std::endl;

  tensorflow::SessionOptions session_options;
  const std::unordered_set<std::string> tags = {"serve"};
  auto model_bundle = std::make_unique<tensorflow::SavedModelBundle>();
  if (auto status = tensorflow::LoadSavedModel(session_options, {},
                                               absl::GetFlag(FLAGS_model_path),
                                               tags, model_bundle.get());
      !status.ok()) {
    ABSL_LOG(ERROR) << "Error loading model: " << status;
    return 1;
  }
  ABSL_LOG(INFO) << "The model is successfully loaded.";

  if (auto status = FreezeSavedModel(session_options, *model_bundle);
      !status.ok()) {
    ABSL_LOG(ERROR) << "Error freezing model: " << status;
    return 1;
  }
  ABSL_LOG(INFO) << "The model is successfully frozen.";
  return 0;
}
