# Copyright 2022 Google LLC
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
load("@rules_buf//buf:defs.bzl", "buf_lint_test")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_descriptor_set", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "bidding_auction_servers_proto",
    srcs = ["bidding_auction_servers.proto"],
    deps = [
        "//api/udf:generate_bid_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_protobuf//:struct_proto",
        "@google_privacysandbox_servers_common//src/logger:logger_proto",
    ],
)

proto_descriptor_set(
    name = "bidding_auction_servers_descriptor_set",
    deps = [":bidding_auction_servers_proto"],
)

cc_proto_library(
    name = "bidding_auction_servers_cc_proto",
    deps = [":bidding_auction_servers_proto"],
)

cc_grpc_library(
    name = "bidding_auction_servers_cc_grpc_proto",
    srcs = [":bidding_auction_servers_proto"],
    grpc_only = True,
    deps = [":bidding_auction_servers_cc_proto"],
)

buf_lint_test(
    name = "base_types_proto_lint",
    size = "small",
    config = ":buf.yaml",
    targets = [":bidding_auction_servers_proto"],
)
