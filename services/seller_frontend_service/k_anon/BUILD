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
    name = "k_anon_utils",
    srcs = ["k_anon_utils.cc"],
    hdrs = [
        "k_anon_utils.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/loggers:request_log_context",
        "//services/common/util:hash_util",
        "//services/seller_frontend_service:report_win_map",
        "//services/seller_frontend_service/data:seller_frontend_data",
        "//services/seller_frontend_service/private_aggregation:private_aggregation_helper",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "k_anon_cache_manager_interface",
    srcs = ["k_anon_cache_manager_interface.h"],
    deps = [
        "//services/common/clients/k_anon_server:k_anon_client",
        "//services/common/metric:server_definition",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
    ],
)

cc_library(
    name = "k_anon_cache_manager_mock",
    srcs = ["k_anon_cache_manager_mock.h"],
    deps = [
        ":k_anon_cache_manager_interface",
        "//services/common/metric:server_definition",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "k_anon_cache_manager",
    srcs = ["k_anon_cache_manager.cc"],
    hdrs = [
        "k_anon_cache_manager.h",
    ],
    deps = [
        ":k_anon_cache_manager_interface",
        ":k_anon_utils",
        "//services/common/clients/k_anon_server:k_anon_client",
        "//services/seller_frontend_service/cache",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "k_anon_cache_manager_test",
    size = "small",
    srcs = [
        "k_anon_cache_manager_test.cc",
    ],
    deps = [
        ":k_anon_cache_manager",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "//services/seller_frontend_service/cache",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/concurrent:executor",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
    ],
)

cc_test(
    name = "k_anon_utils_test",
    size = "small",
    srcs = [
        "k_anon_utils_test.cc",
    ],
    deps = [
        ":k_anon_utils",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto_builder",
        "//services/common/private_aggregation:private_aggregation_test_util",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/core/test/utils",
    ],
)

cc_library(
    name = "constants",
    hdrs = [
        "constants.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)
