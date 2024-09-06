# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_descriptor_set", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "inference_sidecar_proto",
    srcs = ["inference_sidecar.proto"],
    deps = [
    ],
)

proto_descriptor_set(
    name = "inference_sidecar_proto_descriptor_set",
    deps = [":inference_sidecar_proto"],
)

cc_proto_library(
    name = "inference_sidecar_cc_proto",
    deps = [":inference_sidecar_proto"],
)

cc_grpc_library(
    name = "inference_sidecar_cc_grpc_proto",
    srcs = [":inference_sidecar_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":inference_sidecar_cc_proto"],
)

proto_library(
    name = "inference_payload_proto",
    srcs = ["inference_payload.proto"],
)

cc_proto_library(
    name = "inference_payload_cc_proto",
    deps = [":inference_payload_proto"],
)

proto_library(
    name = "model_metadata_proto",
    srcs = ["model_metadata.proto"],
)

cc_proto_library(
    name = "model_metadata_cc_proto",
    deps = [":model_metadata_proto"],
)

proto_library(
    name = "testproto_proto",
    srcs = ["testproto.proto"],
)

cc_proto_library(
    name = "testproto_cc_proto",
    deps = [":testproto_proto"],
)

cc_grpc_library(
    name = "testproto_cc_grpc_proto",
    srcs = [":testproto_proto"],
    grpc_only = True,
    deps = [":testproto_cc_proto"],
)
