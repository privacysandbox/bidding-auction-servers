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

package(default_visibility = ["//:__subpackages__"])
# Description:
#   grpc clients for Fledge Key Value Server
#   https://github.com/WICG/turtledove/blob/main/FLEDGE_Key_Value_Server_API.md

cc_library(
    name = "async_client",
    hdrs = ["async_client.h"],
    # header only library for interface
    linkstatic = True,
    deps = [
        ":client_params_template",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "client_params_template",
    hdrs = ["client_params.h"],
    # header only library for interface
    linkstatic = True,
    deps = [
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "client_factory_template",
    hdrs = ["client_factory.h"],
    # header only library for interface
    linkstatic = True,
    deps = [
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_test(
    name = "client_params_test",
    size = "small",
    srcs =
        [
            "client_params_test.cc",
        ],
    deps = [
        ":client_params_template",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "http_kv_server_gen_url_utils",
    srcs = [
        "http_kv_server/util/generate_url.cc",
    ],
    hdrs = [
        "http_kv_server/util/generate_url.h",
    ],
    deps = [
        "@com_google_absl//absl/strings",
        "@curl",
    ],
)

cc_library(
    name = "buyer_key_value_async_http_client",
    srcs = [
        "http_kv_server/buyer/buyer_key_value_async_http_client.cc",
    ],
    hdrs = [
        "http_kv_server/buyer/buyer_key_value_async_http_client.h",
    ],
    deps = [
        "client_params_template",
        ":async_client",
        ":http_fetcher_async",
        ":http_kv_server_gen_url_utils",
        "//services/common/util:request_metadata",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "seller_key_value_async_http_client",
    srcs = [
        "http_kv_server/seller/seller_key_value_async_http_client.cc",
    ],
    hdrs = [
        "http_kv_server/seller/seller_key_value_async_http_client.h",
    ],
    deps = [
        "client_params_template",
        ":async_client",
        ":http_fetcher_async",
        ":http_kv_server_gen_url_utils",
        "//services/common/util:request_metadata",
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "buyer_key_value_async_http_client_test",
    size = "small",
    srcs = [
        "http_kv_server/buyer/buyer_key_value_async_http_client_test.cc",
    ],
    deps = [
        ":buyer_key_value_async_http_client",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "seller_key_value_async_http_client_test",
    size = "small",
    srcs = [
        "http_kv_server/seller/seller_key_value_async_http_client_test.cc",
    ],
    deps = [
        ":seller_key_value_async_http_client",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "multi_curl_http_fetcher_async",
    srcs = [
        "http/multi_curl_http_fetcher_async.cc",
        "http/multi_curl_request_manager.cc",
        "http/multi_curl_request_manager.h",
    ],
    hdrs = [
        "http/http_fetcher_async.h",
        "http/multi_curl_http_fetcher_async.h",
    ],
    deps = [
        "@com_github_google_glog//:glog",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@curl",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
    ],
)

cc_test(
    name = "multi_curl_http_fetcher_async_test",
    size = "medium",
    srcs = ["http/multi_curl_http_fetcher_async_test.cc"],
    tags = [
        "requires-network",
    ],
    deps = [
        ":multi_curl_http_fetcher_async",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
        "@rapidjson",
    ],
)

cc_library(
    name = "http_fetcher_async",
    hdrs = [
        "http/http_fetcher_async.h",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "bidding_async_client",
    srcs = ["bidding_server/bidding_async_client.cc"],
    hdrs = ["bidding_server/bidding_async_client.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "bidding_async_grpc_client_stub_test",
    size = "large",
    srcs = [
        "bidding_server/bidding_async_grpc_client_stub_test.cc",
    ],
    tags = [
        "flaky",
    ],
    deps = [
        ":bidding_async_client",
        "//services/common/clients/async_grpc:default_async_grpc_client_integration_test_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "scoring_async_client",
    srcs = ["auction_server/scoring_async_client.cc"],
    hdrs = ["auction_server/scoring_async_client.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "//services/common/encryption:crypto_client_wrapper",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/time",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/interface:key_fetcher_manager_interface",
    ],
)

cc_test(
    name = "scoring_async_client_test",
    size = "large",
    srcs = [
        "auction_server/scoring_async_client_test.cc",
    ],
    deps = [
        ":scoring_async_client",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/mock:mock_key_fetcher_manager",
    ],
)

cc_test(
    name = "scoring_async_grpc_client_stub_test",
    size = "large",
    srcs = [
        "auction_server/scoring_async_grpc_client_stub_test.cc",
    ],
    tags = [
        "flaky",
    ],
    deps = [
        ":scoring_async_client",
        "//services/common/clients/async_grpc:default_async_grpc_client_integration_test_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "seller_frontend_async_client",
    srcs = ["seller_frontend_server/seller_frontend_async_client.cc"],
    hdrs = ["seller_frontend_server/seller_frontend_async_client.h"],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//services/common/clients/async_grpc:default_async_grpc_client",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_test(
    name = "seller_frontend_async_grpc_client_stub_test",
    size = "medium",
    srcs = [
        "seller_frontend_server/seller_frontend_async_grpc_client_test.cc",
    ],
    tags = [
        "flaky",
    ],
    deps = [
        ":seller_frontend_async_client",
        "//services/common/clients/async_grpc:default_async_grpc_client_integration_test_utils",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googletest//:gtest",
        "@com_google_googletest//:gtest_main",
    ],
)
