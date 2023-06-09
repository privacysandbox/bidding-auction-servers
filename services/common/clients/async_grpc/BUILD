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
    name = "default_async_grpc_client",
    hdrs = [
        "default_async_grpc_client.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients:async_client",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:crypto_client_wrapper_interface",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/util:error_categories",
        "//services/common/util:status_macros",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:key_fetcher_manager",
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
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/mock:mock_key_fetcher_manager",
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
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)
