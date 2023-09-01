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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = [
    "//services:__subpackages__",
])

cc_library(
    name = "configure_telemetry",
    srcs = [
        "configure_telemetry.cc",
    ],
    hdrs = [
        "configure_telemetry.h",
    ],
    deps = [
        "//services/common/clients/config:config_client_util",
        "@google_privacysandbox_servers_common//src/cpp/telemetry",
        "@io_opentelemetry_cpp//sdk/src/resource",
    ],
)

cc_library(
    name = "telemetry_flag",
    srcs = [
        "telemetry_flag.cc",
    ] + select({
        "//:non_prod_build": ["telemetry_flag_experiment.cc"],
        "//conditions:default": ["telemetry_flag_prod.cc"],
    }),
    hdrs = [
        "telemetry_flag.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":config_cc_proto",
        "//services/common/metric:definition",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "telemetry_flag_test",
    timeout = "short",
    srcs = ["telemetry_flag_test.cc"],
    deps = [
        ":telemetry_flag",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
    ],
)

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
)
