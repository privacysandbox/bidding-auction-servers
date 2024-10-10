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

package(default_visibility = ["//:__subpackages__"])

cc_library(
    name = "kv_async_client",
    srcs = ["kv_async_client.cc"],
    hdrs = ["kv_async_client.h"],
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
        "//services/common/clients:client_params_template",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/util:binary_http_utils",
        "//services/common/util:client_context_util",
        "//services/common/util:oblivious_http_utils",
        "@com_github_google_quiche//quiche:binary_http_unstable_api",
        "@com_github_google_quiche//quiche:oblivious_http_unstable_api",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/communication:encoding_utils",
        "@service_value_key_fledge_privacysandbox//public:constants",
        "@service_value_key_fledge_privacysandbox//public/applications/pas:retrieval_request_builder",
        "@service_value_key_fledge_privacysandbox//public/applications/pas:retrieval_response_parser",
        "@service_value_key_fledge_privacysandbox//public/query/cpp:client_utils",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_grpc",
        "@service_value_key_fledge_privacysandbox//public/query/v2:get_values_v2_cc_proto",
    ],
)

cc_test(
    name = "kv_async_client_test",
    size = "medium",
    srcs = [
        "kv_async_client_test.cc",
    ],
    deps = [
        ":kv_async_client",
        "//services/common/clients/async_grpc:default_async_grpc_client_integration_test_utils",
        "//services/common/clients/async_grpc:default_raw_async_grpc_client_integration_test_utils",
        "//services/common/constants:common_service_flags",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
    ],
)
