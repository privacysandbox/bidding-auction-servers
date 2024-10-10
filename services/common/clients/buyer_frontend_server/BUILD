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
    name = "buyer_frontend_async_client",
    srcs = [
        "buyer_frontend_async_client.cc",
    ],
    hdrs = ["buyer_frontend_async_client.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/chaffing:transcoding_utils",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/util:request_response_constants",
        "@com_github_google_quiche//quiche:quiche_unstable_api",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/encryption/key_fetcher:key_fetcher_manager",
        "@rapidjson",
    ],
)

cc_test(
    name = "buyer_frontend_async_grpc_client_stub_test",
    size = "large",
    srcs = [
        "buyer_frontend_async_grpc_client_stub_test.cc",
    ],
    deps = [
        ":buyer_frontend_async_client",
        "//services/common/chaffing:transcoding_utils",
        "//services/common/clients/async_grpc:default_async_grpc_client_integration_test_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "//services/common/test/utils:test_init",
        "//services/seller_frontend_service:runtime_flags",
        "@com_github_google_quiche//quiche:quiche_unstable_api",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "buyer_frontend_async_client_factory",
    srcs = [
        "buyer_frontend_async_client_factory.cc",
    ],
    hdrs = [
        "buyer_frontend_async_client_factory.h",
    ],
    deps = [
        ":buyer_frontend_async_client",
        "//services/common/clients:async_client",
        "//services/common/clients:client_factory_template",
        "//services/common/concurrent:local_cache",
        "//services/seller_frontend_service/util:startup_param_parser",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_test(
    name = "buyer_frontend_async_client_factory_test",
    size = "small",
    srcs = [
        "buyer_frontend_async_client_factory_test.cc",
    ],
    deps = [
        ":buyer_frontend_async_client_factory",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)
