# Copyright 2024 Google LLC
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
    name = "buyer_frontend_utils",
    srcs = [
        "proto_factory.cc",
    ],
    hdrs = [
        "proto_factory.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/buyer_frontend_service/data:buyer_frontend_data",
        "//services/common/loggers:request_log_context",
        "//services/common/util:json_util",
        "//services/common/util:request_response_constants",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_library(
    name = "bidding_signals",
    srcs = [
        "bidding_signals.cc",
    ],
    hdrs = [
        "bidding_signals.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/buyer_frontend_service/data:buyer_frontend_data",
        "//services/common/util:json_util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
        "@rapidjson",
    ],
)

cc_test(
    name = "bidding_signals_test",
    size = "small",
    testonly = True,
    srcs = [
        "bidding_signals_test.cc",
    ],
    deps = [
        ":bidding_signals",
        "//services/common/test:random",
        "//services/common/test/utils:test_utils",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "proto_factory_test",
    size = "small",
    testonly = True,
    srcs = [
        "proto_factory_test.cc",
    ],
    deps = [
        ":bidding_signals",
        ":buyer_frontend_utils",
        "//services/common/test:random",
        "//services/common/test/utils:test_utils",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "buyer_frontend_test_utils",
    testonly = True,
    srcs = ["buyer_frontend_test_utils.cc"],
    hdrs = ["buyer_frontend_test_utils.h"],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        "//services/buyer_frontend_service/data:buyer_frontend_data",
        "//services/buyer_frontend_service/providers:bidding_signals_providers",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_utils",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
