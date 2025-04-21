
/*
 * Copyright 2025 Google LLC
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

#ifndef SERVICES_INFERENCE_SIDECAR_MODULES_TENSORFLOW_V2_14_0_TENSORFLOW_PROTO_PARSER_H_
#define SERVICES_INFERENCE_SIDECAR_MODULES_TENSORFLOW_V2_14_0_TENSORFLOW_PROTO_PARSER_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "tensorflow/core/framework/tensor.h"
#include "utils/error.h"
#include "utils/request_proto_parser.h"

namespace privacy_sandbox::bidding_auction_servers::inference {

// Converts a TensorProto to a tensorflow::Tensor. Supported data types:
// inference_payload.proto DataTypeProto.
absl::StatusOr<tensorflow::Tensor> ConvertProtoToTensor(
    const TensorProto& tensor);
// Transforms a Tensorflow tensor to an internal ML framework agnostic dense
// tensor protobuff representation into.
absl::StatusOr<TensorProto> ConvertTensorToProto(
    const std::string tensor_name, const tensorflow::Tensor& tensor);

}  // namespace privacy_sandbox::bidding_auction_servers::inference

#endif  // SERVICES_INFERENCE_SIDECAR_MODULES_TENSORFLOW_V2_14_0_TENSORFLOW_PROTO_PARSER_H_
