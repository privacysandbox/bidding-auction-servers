// Copyright 2025 Google LLC
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

#ifndef SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_REQUEST_PROTO_PARSER_H_
#define SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_REQUEST_PROTO_PARSER_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "proto/inference_payload.pb.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

static const absl::flat_hash_map<std::string, DataTypeProto>
    kStringToDataTypeProtoMap = {
        {"FLOAT", DataTypeProto::FLOAT}, {"DOUBLE", DataTypeProto::DOUBLE},
        {"INT8", DataTypeProto::INT8},   {"INT16", DataTypeProto::INT16},
        {"INT32", DataTypeProto::INT32}, {"INT64", DataTypeProto::INT64}};

// Converts JSON inference request to proto format.
// Requests with parsing errors will be stored in
// BatchOrderedInferenceErrorResponse and later merged with response proto.
absl::StatusOr<BatchInferenceRequest> ConvertJsonToProto(
    absl::string_view json_string,
    BatchOrderedInferenceErrorResponse& parsing_errors);

// Converts output response to JSON format.
absl::StatusOr<std::string> ConvertProtoToJson(
    const BatchInferenceResponse& responses);

// Merges the inference response with the parsing errors.
absl::StatusOr<BatchInferenceResponse> MergeBatchResponse(
    const BatchInferenceResponse& response,
    const BatchOrderedInferenceErrorResponse& parsing_errors);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_COMMON_UTILS_REQUEST_PROTO_PARSER_H_
