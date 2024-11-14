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
    name = "request_log_context",
    srcs = select({
        "//:non_prod_build": ["request_log_context_non_prod.cc"],
        "//conditions:default": ["request_log_context_prod.cc"],
    }),
    hdrs = ["request_log_context.h"],
    deps = [
        "//api:bidding_auction_servers_cc_proto",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/random",
        "@com_google_absl//absl/random:bit_gen_ref",
        "@google_privacysandbox_servers_common//src/logger:request_context_impl",
    ],
)

cc_test(
    name = "request_log_context_prod_test",
    timeout = "short",
    srcs =
        select({
            "//:non_prod_build": ["request_log_context_non_prod_test.cc"],
            "//conditions:default": ["request_log_context_prod_test.cc"],
        }),
    deps = [
        ":request_log_context",
        "//services/common/test/utils:test_init",
        "@com_google_googletest//:gtest_main",
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
        "//services/common/loggers:request_log_context",
        "//services/common/loggers:timer",
        "//services/common/util:request_response_constants",
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
    name = "source_location_context",
    srcs = [
        "source_location_context.h",
    ],
    deps = [
        "@google_privacysandbox_servers_common//src/util/status_macro:source_location",
    ],
)
