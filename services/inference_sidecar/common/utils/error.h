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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_ERROR_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_ERROR_H_

#include <string>

#include "absl/strings/string_view.h"
#include "rapidjson/document.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// TODO(b/323592662): Use a proto instead of a C++ struct.
struct Error {
  // Possible types of errors that inference execution could encounter.
  enum ErrorType {
    UNKNOWN = 0,
    INPUT_PARSING = 1,
    MODEL_NOT_FOUND = 2,
    MODEL_EXECUTION = 3,
    OUTPUT_PARSING = 4,
    GRPC = 5
  };

  // The type of error that occurred
  ErrorType error_type = UNKNOWN;

  // Error description
  std::string description;

  // Optional error model_path
  std::string model_path;

  const char* ErrorTypeToString() const {
    static const char* error_type_strs[] = {
        "UNKNOWN",         "INPUT_PARSING",  "MODEL_NOT_FOUND",
        "MODEL_EXECUTION", "OUTPUT_PARSING", "GRPC"};
    return error_type_strs[error_type];
  }
};

// Creates a single error in as a rapidjson::Value. This is used to create the
// final error response JSON string.
rapidjson::Value CreateSingleError(
    rapidjson::Document::AllocatorType& allocator, Error error);

// Creates a batch error JSON string with a single error.
std::string CreateBatchErrorString(Error error,
                                   absl::string_view model_path = "");

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_ERROR_H_
