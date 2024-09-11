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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_INFERENCE_ERROR_CODE_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_INFERENCE_ERROR_CODE_H_

#include <string>

#include "absl/strings/string_view.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

inline constexpr absl::string_view kInferenceUnableToParseRequest =
    "Unable to Parse Request";  // Input request format errors
inline constexpr absl::string_view kInferenceOutputParsingError =
    "Output Parsing Error";  // Failures in parsing or formatting the output

inline constexpr absl::string_view kInferenceTensorInputNameError =
    "Tensor Input Name Error";  // Missing tensor name in inputs
inline constexpr absl::string_view kInferenceModelNotFoundError =
    "Model Not Found Error";  // Model could not be loaded or found
inline constexpr absl::string_view kInferenceModelExecutionError =
    "Model Execution Error";  // Errors during model execution
inline constexpr absl::string_view kInferenceOutputTensorMismatchError =
    "Output Tensor Mismatch Error";  // Output tensor count mismatches
inline constexpr absl::string_view kInferenceSignatureNotFoundError =
    "Signature Not Found Error";  // Model signature missing
inline constexpr absl::string_view kInferenceInputTensorConversionError =
    "Input Tensor Conversion Error";  // Issues converting data to tensors
inline constexpr absl::string_view kInferenceUnknownError =
    "Unknown Error";  // Default error

inline constexpr absl::string_view kInferenceErrorCode[]{
    kInferenceInputTensorConversionError,
    kInferenceModelExecutionError,
    kInferenceModelNotFoundError,
    kInferenceOutputParsingError,
    kInferenceOutputTensorMismatchError,
    kInferenceSignatureNotFoundError,
    kInferenceTensorInputNameError,
    kInferenceUnableToParseRequest,
    kInferenceUnknownError,
};

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_INFERENCE_ERROR_CODE_H_
