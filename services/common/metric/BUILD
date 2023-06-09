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

load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_proto_library", "cc_test")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//visibility:private"])

# ToDo(b/279955396) move everything except :server_definition to common repo

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
)

string_flag(
    name = "metric_build",
    #ToDo(b/279955396): change to prod
    build_setting_default = "experiment",
    values = [
        "prod",
        "experiment",
    ],
)

config_setting(
    name = "prod",
    flag_values = {
        ":metric_build": "prod",
    },
)

config_setting(
    name = "experiment",
    flag_values = {
        ":metric_build": "experiment",
    },
)

cc_library(
    name = "telemetry_flag",
    srcs = [
        "telemetry_flag.cc",
    ] + select({
        ":experiment": ["telemetry_flag_experiment.cc"],
        ":prod": ["telemetry_flag_prod.cc"],
    }),
    hdrs = [
        "telemetry_flag.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":config_cc_proto",
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
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "definition",
    hdrs = [
        "definition.h",
    ],
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_test(
    name = "definition_test",
    timeout = "short",
    srcs = ["definition_test.cc"],
    deps = [
        ":definition",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "metric_router",
    srcs = ["metric_router.cc"],
    hdrs = [
        "metric_router.h",
    ],
    deps = [
        ":definition",
        "//services/common/util:status_macros",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_test(
    name = "metric_router_test",
    timeout = "short",
    srcs = ["metric_router_test.cc"],
    deps = [
        ":metric_router",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
        "@io_opentelemetry_cpp//api",
        "@io_opentelemetry_cpp//exporters/ostream:ostream_metric_exporter",
        "@io_opentelemetry_cpp//sdk/src/metrics",
    ],
)

cc_library(
    name = "context",
    hdrs = [
        "context.h",
    ],
    deps = [
        ":definition",
        ":telemetry_flag",
        "//services/common/util:status_macros",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "context_test",
    timeout = "short",
    srcs = ["context_test.cc"],
    deps = [
        ":context",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "context_map",
    hdrs = [
        "context_map.h",
    ],
    deps = [
        ":context",
        ":metric_router",
        ":telemetry_flag",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "context_map_test",
    timeout = "short",
    srcs = ["context_map_test.cc"],
    deps = [
        ":context_map",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "server_definition",
    hdrs = [
        "server_definition.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":context_map",
    ],
)

cc_test(
    name = "server_definition_test",
    timeout = "short",
    srcs = ["server_definition_test.cc"],
    deps = [
        ":server_definition",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "dp",
    hdrs = [
        "dp.h",
    ],
    deps = [
        ":definition",
        "//services/common/util:status_macros",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_cc_differential_privacy//algorithms:bounded-sum",
    ],
)

cc_test(
    name = "dp_test",
    timeout = "short",
    srcs = ["dp_test.cc"],
    deps = [
        ":dp",
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
    ],
)
