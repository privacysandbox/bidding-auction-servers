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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "file_util",
    srcs = ["file_util.cc"],
    hdrs = ["file_util.h"],
    deps = [
        "//proto:inference_sidecar_cc_proto",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_library(
    name = "json_util",
    hdrs = [
        "json_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/status:statusor",
        "@rapidjson",
    ],
)

cc_library(
    name = "request_parser",
    srcs = [
        "request_parser.cc",
    ],
    hdrs = [
        "request_parser.h",
    ],
    deps = [
        ":json_util",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "request_parser_test",
    size = "small",
    timeout = "short",
    srcs = ["request_parser_test.cc"],
    deps = [
        ":request_parser",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "cpu",
    srcs = ["cpu.cc"],
    hdrs = ["cpu.h"],
    deps = [
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "cpu_test",
    size = "small",
    srcs = ["cpu_test.cc"],
    deps = [
        ":cpu",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "log",
    hdrs = ["log.h"],
    deps = [
        "//proto:inference_sidecar_cc_proto",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "log_test",
    size = "small",
    srcs = ["log_test.cc"],
    deps = [
        ":log",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
