// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "services/bidding_service/inference/inference_flags.h"

#include <optional>
#include <string>

#include "absl/flags/flag.h"

ABSL_FLAG(std::optional<std::string>, inference_sidecar_binary_path,
          std::nullopt, "The binary path of the inference sidecar.");
// Used to load local models for debugging purposes. In the prod setting, this
// flag is not used, and we fetch models from cloud buckets.
ABSL_FLAG(std::optional<std::string>, inference_model_local_paths, std::nullopt,
          "Comma separated list of inference model paths to read from the "
          "local disk. It's mainly used for testing.");
ABSL_FLAG(std::optional<std::string>, inference_model_bucket_name, std::nullopt,
          "Bucket name for fetching models.");
// TODO(b/330942801): Clean up the flag.
ABSL_FLAG(std::optional<std::string>, inference_model_bucket_paths,
          std::nullopt,
          "Comma separated list of bucket paths. Used to specify a list of "
          "directories to fetch the blobs from.");
ABSL_FLAG(std::optional<std::string>, inference_model_config_path, std::nullopt,
          "Path to the model config file stored in the cloud bucket.");
ABSL_FLAG(std::optional<int64_t>, inference_model_fetch_period_ms, std::nullopt,
          "Period to fetch new models from the cloud bucket in milliseconds");
ABSL_FLAG(std::optional<int64_t>, inference_sidecar_rlimit_mb, 0,
          "Rlimit-based memory limit for the inference sidecar");
// The JSON string should adhere to the following format:
// {
//    "num_interop_threads": <integer_value>,
//    "num_intraop_threads": <integer_value>,
//    "module_name": <string_value>,
//    "cpuset": <an array of integer values>,
//    ...
// }
// where each property corresponds to a field of the
// InferenceSidecarRuntimeConfig proto in
// services/inference_sidecar/common/proto/inference_sidecar.proto.
ABSL_FLAG(std::optional<std::string>, inference_sidecar_runtime_config,
          std::nullopt,
          "JSON string configurations for the inference sidecar runtime.");
