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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(
    default_visibility = [
        "//services:__subpackages__",
    ],
    licenses = ["notice"],
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

cc_test(
    name = "json_util_test",
    size = "small",
    srcs = [
        "json_util_test.cc",
    ],
    deps = [
        ":json_util",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "request_metadata",
    srcs = [
        "request_metadata.h",
    ],
    hdrs = [
        "request_metadata.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//services/common/clients:async_client",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "request_metadata_test",
    size = "small",
    srcs = [
        "request_metadata_test.cc",
    ],
    deps = [
        ":request_metadata",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "status_util",
    srcs = [
        "status_util.cc",
    ],
    hdrs = [
        "status_util.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "status_util_test",
    size = "small",
    srcs = [
        "status_util_test.cc",
    ],
    deps = [
        ":status_util",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        ":status_builder",
        ":status_util",
    ],
)

cc_test(
    name = "status_macros_test",
    size = "small",
    srcs = [
        "status_macros_test.cc",
    ],
    deps = [
        ":status_macros",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "source_location",
    hdrs = ["source_location.h"],
)

cc_library(
    name = "examine_stack",
    srcs = [
        "examine_stack.cc",
    ],
    hdrs = [
        "examine_stack.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/debugging:stacktrace",
        "@com_google_absl//absl/debugging:symbolize",
    ],
)

cc_library(
    name = "status_builder",
    srcs = ["status_builder.cc"],
    hdrs = ["status_builder.h"],
    deps = [
        ":examine_stack",
        ":source_location",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:absl_log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "status_builder_test",
    size = "small",
    srcs = ["status_builder_test.cc"],
    deps = [
        ":status_builder",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log:scoped_mock_log",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "scoped_cbor",
    hdrs = [
        "scoped_cbor.h",
    ],
    deps = [
        "@libcbor//:cbor",
    ],
)

cc_library(
    name = "request_response_constants",
    hdrs = ["request_response_constants.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = ["@com_google_absl//absl/strings"],
)

cc_test(
    name = "scoped_cbor_test",
    size = "small",
    srcs = [
        "scoped_cbor_test.cc",
    ],
    deps = [
        ":scoped_cbor",
        "@com_google_absl//absl/flags:flag",
        "@com_google_googletest//:gtest_main",
        "@libcbor//:cbor",
    ],
)

cc_library(
    name = "context_logger",
    srcs = ["context_logger.cc"],
    hdrs = ["context_logger.h"],
    deps = [
        ":source_location",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "context_logger_test",
    size = "small",
    srcs = [
        "context_logger_test.cc",
    ],
    deps = [
        ":context_logger",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "error_categories",
    hdrs = ["error_categories.h"],
)

cc_library(
    name = "error_reporter",
    hdrs = ["error_reporter.h"],
    deps = [
        ":error_categories",
        ":source_location",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "error_accumulator",
    srcs = ["error_accumulator.cc"],
    hdrs = ["error_accumulator.h"],
    deps = [
        ":context_logger",
        ":error_categories",
        ":error_reporter",
        ":source_location",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "error_accumulator_test",
    size = "small",
    srcs = [
        "error_accumulator_test.cc",
    ],
    deps = [
        ":error_accumulator",
        ":error_categories",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "post_auction_signals",
    hdrs = ["post_auction_signals.h"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "reporting_util",
    srcs = ["reporting_util.cc"],
    hdrs = [
        "reporting_util.h",
    ],
    deps = [
        ":post_auction_signals",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/http:http_fetcher_async",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "reporting_util_test",
    size = "small",
    srcs = ["reporting_util_test.cc"],
    deps = [
        ":reporting_util",
        "//services/common/test:random",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
