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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "timer",
    srcs = ["timer.cc"],
    hdrs = ["timer.h"],
    deps = [
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "benchmarking_logger",
    srcs = ["benchmarking_logger.cc"],
    hdrs = [
        "benchmarking_logger.h",
        "no_ops_logger.h",
    ],
    deps = [
        "//services/common/loggers:request_context_logger",
        "//services/common/loggers:timer",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "build_input_process_response_benchmarking_logger",
    srcs = ["build_input_process_response_benchmarking_logger.cc"],
    hdrs = ["build_input_process_response_benchmarking_logger.h"],
    deps = [
        ":benchmarking_logger",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "request_context_logger",
    hdrs = [
        "request_context_logger.h",
    ],
    defines = select({
        "//:non_prod_build": ["PS_LOG_NON_PROD=1"],
        "//conditions:default": [],
    }),
    deps = [
        "@com_google_absl//absl/log:absl_log",
    ],
)

cc_test(
    name = "request_context_logger_test",
    timeout = "short",
    srcs = ["request_context_logger_test.h"] +
           select({
               "//:non_prod_build": ["request_context_logger_non_prod_test.cc"],
               "//conditions:default": ["request_context_logger_prod_test.cc"],
           }),
    deps = [
        ":request_context_logger",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "request_context_impl",
    srcs = [
        "request_context_impl.cc",
    ],
    hdrs = [
        "request_context_impl.h",
    ],
    deps = [
        ":request_context_logger",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/util:request_response_constants",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@google_privacysandbox_servers_common//src/cpp/util/status_macro:source_location",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_test(
    name = "request_context_impl_test",
    timeout = "short",
    srcs = [
        "request_context_impl_test.h",
        "request_context_logger_test.h",
    ] + select({
        "//:non_prod_build": ["request_context_impl_non_prod_test.cc"],
        "//conditions:default": ["request_context_impl_prod_test.cc"],
    }),
    deps = [
        ":request_context_impl",
        "//services/common/test/utils:proto_utils",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@io_opentelemetry_cpp//exporters/ostream:ostream_log_record_exporter",
    ],
)
