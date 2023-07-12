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

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "runtime_flags",
    hdrs = [
        "runtime_flags.h",
    ],
    deps = [
        "//services/common/constants:common_service_flags",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "buyer_frontend_data",
    hdrs = [
        "data/bidding_signals.h",
        "data/get_bids_config.h",
    ],
    deps = [
        "//api:bidding_auction_servers_cc_grpc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "buyer_frontend_utils",
    srcs = [
        "util/proto_factory.cc",
    ],
    hdrs = [
        "util/proto_factory.h",
    ],
    deps = [
        ":buyer_frontend_data",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "buyer_frontend_utils_test",
    size = "small",
    srcs = [
        "util/proto_factory_test.cc",
    ],
    deps = [
        "buyer_frontend_utils",
        "//services/common/test:random",
        "@com_github_google_glog//:glog",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "bidding_signals_providers",
    srcs = [
        "providers/http_bidding_signals_async_provider.cc",
    ],
    hdrs = [
        "providers/bidding_signals_async_provider.h",
        "providers/http_bidding_signals_async_provider.h",
    ],
    deps = [
        ":buyer_frontend_data",
        "//services/common/clients:buyer_key_value_async_http_client",
        "//services/common/clients:client_factory_template",
        "//services/common/providers:async_provider",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

cc_test(
    name = "bidding_signals_providers_test",
    size = "small",
    srcs = [
        "providers/http_bidding_signals_async_provider_test.cc",
    ],
    deps = [
        "bidding_signals_providers",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "get_bids_unary_reactor",
    srcs = [
        "get_bids_unary_reactor.cc",
    ],
    hdrs = [
        "get_bids_unary_reactor.h",
    ],
    deps = [
        ":bidding_signals_providers",
        ":buyer_frontend_utils",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients/bidding_server:async_client",
        "//services/common/constants:user_error_strings",
        "//services/common/loggers:benchmarking_logger",
        "//services/common/loggers:build_input_process_response_benchmarking_logger",
        "//services/common/metric:server_definition",
        "//services/common/util:context_logger",
        "//services/common/util:request_metadata",
        "//services/common/util:request_response_constants",
        "@com_github_google_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "buyer_frontend_service",
    srcs = [
        "buyer_frontend_service.cc",
    ],
    hdrs = [
        "buyer_frontend_service.h",
    ],
    visibility = [
        "//services:__subpackages__",
        "//tools/secure_invoke:__subpackages__",
    ],
    deps = [
        ":bidding_signals_providers",
        ":buyer_frontend_utils",
        ":get_bids_unary_reactor",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients/bidding_server:async_client",
        "//services/common/clients/http:multi_curl_http_fetcher_async",
        "//services/common/metric:server_definition",
        "@com_github_google_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@control_plane_shared//cc/public/cpio/interface:cpio",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/cpp/telemetry",
    ],
)

cc_test(
    name = "get_bids_unary_reactor_test",
    size = "small",
    srcs = [
        "get_bids_unary_reactor_test.cc",
    ],
    deps = [
        ":buyer_frontend_service",
        "//services/common/encryption:mock_crypto_client_wrapper",
        "//services/common/test:mocks",
        "//services/common/test:random",
        "@com_google_googletest//:gtest_main",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/mock:mock_key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:key_fetcher_manager",
    ],
)

cc_binary(
    name = "server",
    srcs = ["buyer_frontend_main.cc"],
    linkopts = [
        "-Wl,-rpath,\\$$ORIGIN/../lib",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bidding_signals_providers",
        ":buyer_frontend_service",
        ":runtime_flags",
        "//api:bidding_auction_servers_cc_grpc_proto",
        "//api:bidding_auction_servers_cc_proto",
        "//services/common/clients/config:config_client",
        "//services/common/clients/config:config_client_util",
        "//services/common/concurrent:local_cache",
        "//services/common/encryption:crypto_client_factory",
        "//services/common/encryption:key_fetcher_factory",
        "//services/common/telemetry:configure_telemetry",
        "//services/common/util:status_macros",
        "@aws_sdk_cpp//:core",
        "@com_github_google_glog//:glog",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",  # for grpc_cli
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@google_privacysandbox_servers_common//src/cpp/concurrent:executor",
        "@google_privacysandbox_servers_common//src/cpp/encryption/key_fetcher/src:key_fetcher_manager",
        "@google_privacysandbox_servers_common//src/cpp/telemetry",
    ],
)
