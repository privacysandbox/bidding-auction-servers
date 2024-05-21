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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_FILE_UTIL_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_FILE_UTIL_H_

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "proto/inference_sidecar.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Reads the contents of a provided model and populates the given
// RegisterModelRequest.
// TODO(b/322106194): Depending on the final implementation of model loading,
// this function might need to read from tmpfs or ramfs.
absl::Status PopulateRegisterModelRequest(absl::string_view path,
                                          RegisterModelRequest& request,
                                          bool log_on_error = false);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_FILE_UTIL_H_
