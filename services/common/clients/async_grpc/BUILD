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

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "request_config",
    hdrs = [
        "request_config.h",
    ],
    deps = ["//services/common/compression:compression_utils"],
)

cc_library(
    name = "default_async_grpc_client",
    srcs = ["default_async_grpc_client.cc"],
    hdrs = [
        "default_async_grpc_client.h",
    ],
    copts =
        select(
            {
                "@google_privacysandbox_servers_common//:aws_instance": [
                    "-DCLOUD_PLATFORM_AWS=1",
                ],
                "@google_privacysandbox_servers_common//:gcp_instance": [
                    "-DCLOUD_PLATFORM_GCP=1",
                ],
                "@google_privacysandbox_servers_common//:local_instance": [
                    "-DCLOUD_PLATFORM_LOCAL=1",
                ],
            },
            no_match_error = "Please build for GCP, AWS, or local.",
        ),
    deps = [
        ":grpc_client_utils",
        ":request_config",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients:async_client",
        "//services/common/constants:common_constants",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/loggers:request_log_context",
        "//services/common/util:client_context_util",
        "//services/common/util:error_categories",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)

cc_test(
    name = "default_async_grpc_client_integration_test_utils",
    size = "medium",
    srcs = ["default_async_grpc_client_stub_test.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_test(
    name = "bidding_async_grpc_client_integration_test_utils",
    size = "medium",
    srcs = ["bidding_async_grpc_client_stub_test.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_test(
    name = "default_async_grpc_client_test",
    size = "medium",
    srcs = ["default_async_grpc_client_test.cc"],
    deps = [
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "default_raw_async_grpc_client_integration_test_utils",
    size = "medium",
    srcs = ["default_raw_async_grpc_client_stub_test.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_library(
    name = "grpc_client_utils",
    hdrs = [
        "grpc_client_utils.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/util:hpke_utils",
        "@com_google_absl//absl/status",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/logger:request_context_logger",
        "@google_privacysandbox_servers_common//src/util/status_macro:status_macros",
    ],
)
