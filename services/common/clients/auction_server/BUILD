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
    name = "async_client",
    srcs = ["scoring_async_client.cc"],
    hdrs = ["scoring_async_client.h"],
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
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/encryption:crypto_client_wrapper",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/interface:key_fetcher_manager_interface",
    ],
)

cc_test(
    name = "async_client_test",
    size = "large",
    srcs = [
        "scoring_async_client_test.cc",
    ],
    deps = [
        ":async_client",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test/utils:test_init",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_test(
    name = "async_grpc_client_stub_test",
    size = "large",
    srcs = [
        "scoring_async_grpc_client_stub_test.cc",
    ],
    deps = [
        ":async_client",
        "//services/common/clients/async_grpc:default_async_grpc_client_integration_test_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
