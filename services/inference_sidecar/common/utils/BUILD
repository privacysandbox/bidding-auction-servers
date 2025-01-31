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
        ":error",
        ":inference_error_code",
        ":inference_metric_util",
        ":json_util",
        "//proto:inference_sidecar_cc_proto",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_library(
    name = "resource_size_utils",
    srcs = [
        "resource_size_utils.cc",
    ],
    hdrs = [
        "resource_size_utils.h",
    ],
    deps = [
    ],
)

cc_test(
    name = "resource_size_utils_test",
    size = "small",
    timeout = "short",
    srcs = ["resource_size_utils_test.cc"],
    deps = [
        ":resource_size_utils",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "request_parser_test",
    size = "small",
    timeout = "short",
    srcs = ["request_parser_test.cc"],
    deps = [
        ":error",
        ":inference_error_code",
        ":request_parser",
        ":test_util",
        "//proto:inference_sidecar_cc_proto",
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

cc_library(
    name = "inference_error_code",
    hdrs = ["inference_error_code.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "inference_metric_util",
    srcs = ["inference_metric_util.cc"],
    hdrs = ["inference_metric_util.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":inference_error_code",
        "//proto:inference_sidecar_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "inference_metric_util_test",
    size = "small",
    srcs = ["inference_metric_util_test.cc"],
    deps = [
        ":inference_error_code",
        ":inference_metric_util",
        "//proto:inference_sidecar_cc_proto",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "error",
    srcs = ["error.cc"],
    hdrs = ["error.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/strings",
        "@rapidjson",
    ],
)

cc_test(
    name = "error_test",
    size = "small",
    srcs = ["error_test.cc"],
    deps = [
        ":error",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "test_util",
    hdrs = ["test_util.h"],
    deps = [
        "//proto:inference_sidecar_cc_proto",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "tcmalloc",
    hdrs = [
        "tcmalloc.h",
    ],
    deps = [
        "@com_google_tcmalloc//tcmalloc:malloc_extension",
    ],
)
