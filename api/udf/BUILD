# Copyright 2024 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load(
    "@google_privacysandbox_servers_common//src/roma/tools/api_plugin:roma_api.bzl",
    "declare_roma_api",
    "roma_byob_sdk",
)
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "generate_bid_proto",
    srcs = ["generate_bid.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@google_privacysandbox_servers_common//apis/privacysandbox/apis/roma/app_api/v1:options_proto",
    ],
)

cc_proto_library(
    name = "generate_bid_cc_proto",
    visibility = ["//visibility:public"],
    deps = [
        ":generate_bid_proto",
    ],
)

generate_bid_api = declare_roma_api(
    cc_protos = [":generate_bid_cc_proto"],
    proto_basename = "generate_bid",
    protos = [":generate_bid_proto"],
)

roma_byob_sdk(
    name = "generate_bid_sdk",
    srcs = [],
    roma_app_api = generate_bid_api,
    visibility = ["//visibility:public"],
)
